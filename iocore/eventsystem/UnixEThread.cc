/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

//////////////////////////////////////////////////////////////////////
//
// The EThread Class
//
/////////////////////////////////////////////////////////////////////
#include "P_EventSystem.h"

#if HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

struct AIOCallback;

#define NO_HEARTBEAT -1
#define THREAD_MAX_HEARTBEAT_MSECONDS 60

// !! THIS MUST BE IN THE ENUM ORDER !!
char const *const EThread::STAT_NAME[] = {"proxy.process.eventloop.count",      "proxy.process.eventloop.events",
                                          "proxy.process.eventloop.events.min", "proxy.process.eventloop.events.max",
                                          "proxy.process.eventloop.wait",       "proxy.process.eventloop.time.min",
                                          "proxy.process.eventloop.time.max"};

int const EThread::SAMPLE_COUNT[N_EVENT_TIMESCALES] = {10, 100, 1000};

bool shutdown_event_system = false;

int thread_max_heartbeat_mseconds = THREAD_MAX_HEARTBEAT_MSECONDS;

EThread::EThread()
{
  memset(thread_private, 0, PER_THREAD_DATA);
}

EThread::EThread(ThreadType att, int anid) : id(anid), tt(att)
{
  ethreads_to_be_signalled = (EThread **)ats_malloc(MAX_EVENT_THREADS * sizeof(EThread *));
  memset(ethreads_to_be_signalled, 0, MAX_EVENT_THREADS * sizeof(EThread *));
  memset(thread_private, 0, PER_THREAD_DATA);
#if HAVE_EVENTFD
  evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evfd < 0) {
    if (errno == EINVAL) { // flags invalid for kernel <= 2.6.26
      evfd = eventfd(0, 0);
      if (evfd < 0) {
        Fatal("EThread::EThread: %d=eventfd(0,0),errno(%d)", evfd, errno);
      }
    } else {
      Fatal("EThread::EThread: %d=eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC),errno(%d)", evfd, errno);
    }
  }
#elif TS_USE_PORT
/* Solaris ports requires no crutches to do cross thread signaling.
 * We'll just port_send the event straight over the port.
 */
#else
  ink_release_assert(pipe(evpipe) >= 0);
  fcntl(evpipe[0], F_SETFD, FD_CLOEXEC);
  fcntl(evpipe[0], F_SETFL, O_NONBLOCK);
  fcntl(evpipe[1], F_SETFD, FD_CLOEXEC);
  fcntl(evpipe[1], F_SETFL, O_NONBLOCK);
#endif
}

EThread::EThread(ThreadType att, Event *e) : tt(att), start_event(e)
{
  ink_assert(att == DEDICATED);
  memset(thread_private, 0, PER_THREAD_DATA);
}

// Provide a destructor so that SDK functions which create and destroy
// threads won't have to deal with EThread memory deallocation.
EThread::~EThread()
{
  if (n_ethreads_to_be_signalled > 0) {
    flush_signals(this);
  }
  ats_free(ethreads_to_be_signalled);
  // TODO: This can't be deleted ....
  // delete[]l1_hash;
}

// 判断该 EThread 是否支持处理的指定类型的 event
bool
EThread::is_event_type(EventType et)
{
  return (event_types & (1 << static_cast<int>(et))) != 0;
}

// EThread 的 event_types 成员是一个按位操作的状态值，可通过该
// 方法让一个 EThread 同时处理多种类型的 event.
void
EThread::set_event_type(EventType et)
{
  event_types |= (1 << static_cast<int>(et));
}

void
EThread::process_event(Event *e, int calling_code)
{
  ink_assert((!e->in_the_prot_queue && !e->in_the_priority_queue));
  // 尝试获取锁
  MUTEX_TRY_LOCK(lock, e->mutex, this);
  // 判断是否拿到锁
  if (!lock.is_locked()) {
    // 如果没有拿到锁，则把 event 重新丢回外部本地队列
    e->timeout_at = cur_time + DELAY_FOR_RETRY;
    EventQueueExternal.enqueue_local(e);
    // 返回到 execute()
  } else {
    // 如果拿到了锁，首先判断当前 event 是否已经取消
    if (e->cancelled) {
      // 如果被取消了，则释放 event
      free_event(e);
      // 返回到 execute()
      return;
    }
    // 准备回调 Cont->handleEvent
    Continuation *c_temp = e->continuation;
    // 注意：在回调期间，Cont->mutex 是被当前 EThread 锁定的
    // Make sure that the contination is locked before calling the handler
    e->continuation->handleEvent(calling_code, e);
    ink_assert(!e->in_the_priority_queue);
    ink_assert(c_temp == e->continuation);
    // 提前释放锁
    MUTEX_RELEASE(lock);
    // 如果该 event 是周期性执行的，通过 schedule_every 添加
    if (e->period) {
      // 不在保护队列，也不在优先级队列（就是既不在外部队列，也不在内部队列）
      //   通常在调用 process_event 之前从队列中 dequeue 了，就应该不会在队列里了
      //   但是，在 Cont->handleEvent 可能会调用了其他操作导致 event 被重新放回队列
      if (!e->in_the_prot_queue && !e->in_the_priority_queue) {
        if (e->period < 0) {
          // 小于零表示这是一个隐性队列内的 event，不用对 timeout_at 的值进行重新计算
          e->timeout_at = e->period;
        } else {
          // 对下一次执行时间 timeout_at 的值进行重新计算
          this->get_hrtime_updated();
          e->timeout_at = cur_time + e->period;
          if (e->timeout_at < cur_time) {
            e->timeout_at = cur_time;
          }
        }
        // 将重新设置好 timeout_at 的 event 放入外部本地队列
        EventQueueExternal.enqueue_local(e);
      }
    } else if (!e->in_the_prot_queue && !e->in_the_priority_queue) {
      // 不是周期性 event，也不在保护队列，又不在优先级队列，则释放 event
      free_event(e);
    }
  }
}

void
EThread::process_queue(Que(Event, link) * NegativeQueue, int *ev_count, int *nq_count)
{
  Event *e;

  // 将事件从外部队列中安全的移动到本地队列
  // Move events from the external thread safe queues to the local queue.
  EventQueueExternal.dequeue_external();

  // execute all the available external events that have
  // already been dequeued
  // 遍历: 外部本地队列
  // dequeue_local() 方法每次从外部本地队列取出一个事件
  while ((e = EventQueueExternal.dequeue_local())) {
    ++(*ev_count);
    // 事件被异步取消时
    if (e->cancelled) {
      free_event(e);
    // 立即执行的事件
    } else if (!e->timeout_at) { // IMMEDIATE
      ink_assert(e->period == 0);
      // 通过 process_event 回调状态机
      process_event(e, e->callback_event);
    // 周期执行的事件
    } else if (e->timeout_at > 0) { // INTERVAL
      // 放入内部队列
      EventQueue.enqueue(e, cur_time);
    // 负事件
    } else { // NEGATIVE
      Event *p = nullptr;
      Event *a = NegativeQueue->head;
      // 注意这里 timeout_at 的值是小于 0 的负数
      // 按照从小到大的顺序排列
      // 如果遇到相同的值，则后插入的事件在前面
      while (a && a->timeout_at > e->timeout_at) {
        p = a;
        a = a->link.next;
      }
      // 放入隐性队列（负队列）
      if (!a) {
        NegativeQueue->enqueue(e);
      } else {
        NegativeQueue->insert(e, p);
      }
    }
    ++(*nq_count);
  }
}

void
EThread::execute_regular()
{
  Event *e;
  Que(Event, link) NegativeQueue;
  ink_hrtime next_time = 0;
  ink_hrtime delta     = 0;    // time spent in the event loop
  ink_hrtime loop_start_time;  // Time the loop started.
  ink_hrtime loop_finish_time; // Time at the end of the loop.

  // Track this so we can update on boundary crossing.
  EventMetrics *prev_metric = this->prev(metrics + (ink_get_hrtime_internal() / HRTIME_SECOND) % N_EVENT_METRICS);

  int nq_count = 0;
  int ev_count = 0;

  // A statically initialized instance we can use as a prototype for initializing other instances.
  static EventMetrics METRIC_INIT;

  // give priority to immediate events
  // 设计目的：优先处理立即执行的事件
  for (;;) {
    if (unlikely(shutdown_event_system == true)) {
      return;
    }

    loop_start_time = Thread::get_hrtime_updated();
    nq_count        = 0; // count # of elements put on negative queue.
    ev_count        = 0; // # of events handled.

    current_metric = metrics + (loop_start_time / HRTIME_SECOND) % N_EVENT_METRICS;
    if (current_metric != prev_metric) {
      // Mixed feelings - really this shouldn't be needed, but just in case more than one entry is
      // skipped, clear them all.
      do {
        memcpy((prev_metric = this->next(prev_metric)), &METRIC_INIT, sizeof(METRIC_INIT));
      } while (current_metric != prev_metric);
      current_metric->_loop_time._start = loop_start_time;
    }
    ++(current_metric->_count);

    process_queue(&NegativeQueue, &ev_count, &nq_count);

    // 遍历：内部队列（优先级队列）
    bool done_one;
    do {
      done_one = false;
      // execute all the eligible internal events
      // check_ready 方法将优先级队列内的多个子队列进行重排（reschedule）
      // 使每个子队列容纳的事件的执行时间符合每一个子队列的要求
      EventQueue.check_ready(cur_time, this);
      // dequeue_ready 方法只操作 0 号子队列，这里面的事件需要在 5ms 内执行
      while ((e = EventQueue.dequeue_ready(cur_time))) {
        ink_assert(e);
        ink_assert(e->timeout_at > 0);
        if (e->cancelled) {
          free_event(e);
        } else {
          // 本次循环处理了一个事件，设置 done_one 为 true，表示继续遍历内部队列
          done_one = true;
          process_event(e, e->callback_event);
        }
      }
      /* 
       * 每次循环都会把 0 号子队列里面的事件全部处理完
       * 如果本次对 0 号子队列的遍历至少处理了一个事件，那么
       *   这个处理过程是会花掉时间的，但是事件若是被取消了则不会花掉多少时间，所以不记录在内；
       *   此时，1 号子队列里面的事件可能已经到了需要执行的时间，
       *   或者，1 号子队列里面的事件已经超时了，
       * 所以要通过 do - while(done_one) 循环来再次整理优先级队列,
       *   然后再次处理 0 号子队列，以保证事件的按时执行.
       * 如果本次循环一个事件都没有处理，
       *   那就说明在整理队列之后，0 号子队列仍然是空队列，
       *   这表示内部队列中最近一个需要执行的事件在 5ms 之后
       *   那就不再需要对内部队列进行遍历，结束 do-while(done_one) 循环
       * 注意：此处或许有一个假设，那就是每个 REGULAR ETHREAD 的大循环，运行时间为 5ms，
       *   如果一次循环的时间超过了 5ms，内部队列的事件就可能会超时.
       */
    } while (done_one);

    // execute any negative (poll) events
    // 遍历：隐性队列（如果隐性队列不为空的话）
    if (NegativeQueue.head) {
      process_queue(&NegativeQueue, &ev_count, &nq_count);

      // execute poll events
      // 执行隐性队列里的 polling 事件，每次取一个事件，通过 process_event 呼叫状态机，目前：
      // 对于 TCP，状态机是 NetHandler::mainNetEvent
      // 对于 UDP，状态机是 PollCont::pollEvent
      while ((e = NegativeQueue.dequeue())) {
        process_event(e, EVENT_POLL);
      }
    }

    // 没有负事件，那就是只有周期性事件需要执行，那么此时就需要节省 CPU 资源避免空转
    // 通过内部队列里最早需要执行事件的时间距离当前时间的差值，得到可以休眠的时间
    next_time             = EventQueue.earliest_timeout();
    ink_hrtime sleep_time = next_time - Thread::get_hrtime_updated();
    if (sleep_time > 0) {
      // 将该休眠时间与最大休眠时间比较后取小的
      sleep_time = std::min(sleep_time, HRTIME_MSECONDS(thread_max_heartbeat_mseconds));
      ++(current_metric->_wait);
    } else {
      sleep_time = 0;
    }

    // 触发 signals，向其它线程通知事件
    // 该方法有可能会阻塞
    if (n_ethreads_to_be_signalled) {
      flush_signals(this);
    }

    // 阻塞等待 sleep_time
    tail_cb->waitForActivity(sleep_time);

    // loop cleanup
    loop_finish_time = this->get_hrtime_updated();
    delta            = loop_finish_time - loop_start_time;

    // This can happen due to time of day adjustments (which apparently happen quite frequently). I
    // tried using the monotonic clock to get around this but it was *very* stuttery (up to hundreds
    // of milliseconds), far too much to be actually used.
    if (delta > 0) {
      if (delta > current_metric->_loop_time._max) {
        current_metric->_loop_time._max = delta;
      }
      if (delta < current_metric->_loop_time._min) {
        current_metric->_loop_time._min = delta;
      }
    }
    if (ev_count < current_metric->_events._min) {
      current_metric->_events._min = ev_count;
    }
    if (ev_count > current_metric->_events._max) {
      current_metric->_events._max = ev_count;
    }
    current_metric->_events._total += ev_count;
  }
}

//
// void  EThread::execute()
//
// Execute loops forever on:
// Find the earliest event.
// Sleep until the event time or until an earlier event is inserted
// When its time for the event, try to get the appropriate continuation
// lock. If successful, call the continuation, otherwise put the event back
// into the queue.
//

/**
 * EThread::execute() 由 switch 语句分成多个部分：
 *   - 第一部分是 REGULAR 类型的处理
 *       - 它是一个无限循环内的代码，持续扫描/遍历多个队列，并回调 Event 内部 Cont 的 handler
 *   - 第二部分是 DEDICATED 类型的处理
 */

void
EThread::execute()
{
  // Do the start event first.
  // coverity[lock]
  // 在进行任何调度之前调用的初始事件
  if (start_event) {
    // 获取锁
    MUTEX_TAKE_LOCK_FOR(start_event->mutex, this, start_event->continuation);
    start_event->continuation->handleEvent(EVENT_IMMEDIATE, start_event);
    // 释放锁
    MUTEX_UNTAKE_LOCK(start_event->mutex, this);
    // 释放该事件
    free_event(start_event);
    start_event = nullptr;
  }

  switch (tt) {
  case REGULAR: {
    this->execute_regular();
    break;
  }
  case DEDICATED: {
    break;
  }
  default:
    ink_assert(!"bad case value (execute)");
    break;
  } /* End switch */
  // coverity[missing_unlock]
}

EThread::EventMetrics &
EThread::EventMetrics::operator+=(EventMetrics const &that)
{
  this->_events._max = std::max(this->_events._max, that._events._max);
  this->_events._min = std::min(this->_events._min, that._events._min);
  this->_events._total += that._events._total;
  this->_loop_time._min = std::min(this->_loop_time._min, that._loop_time._min);
  this->_loop_time._max = std::max(this->_loop_time._max, that._loop_time._max);
  this->_count += that._count;
  this->_wait += that._wait;
  return *this;
}

void
EThread::summarize_stats(EventMetrics summary[N_EVENT_TIMESCALES])
{
  // Accumulate in local first so each sample only needs to be processed once,
  // not N_EVENT_TIMESCALES times.
  EventMetrics sum;

  // To avoid race conditions, we back up one from the current metric block. It's close enough
  // and won't be updated during the time this method runs so it should be thread safe.
  EventMetrics *m = this->prev(current_metric);

  for (int t = 0; t < N_EVENT_TIMESCALES; ++t) {
    int count = SAMPLE_COUNT[t];
    if (t > 0) {
      count -= SAMPLE_COUNT[t - 1];
    }
    while (--count >= 0) {
      if (0 != m->_loop_time._start) {
        sum += *m;
      }
      m = this->prev(m);
    }
    summary[t] += sum; // push out to return vector.
  }
}
