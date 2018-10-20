/** @file

  FIFO queue

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

  @section details Details

  ProtectedQueue implements a FIFO queue with the following functionality:
    -# Multiple threads could be simultaneously trying to enqueue and
      dequeue. Hence the queue needs to be protected with mutex.
    -# In case the queue is empty, dequeue() sleeps for a specified amount
      of time, or until a new element is inserted, whichever is earlier.

*/

#include "P_EventSystem.h"

// The protected queue is designed to delay signaling of threads
// until some amount of work has been completed on the current thread
// in order to prevent excess context switches.
//
// Defining EAGER_SIGNALLING disables this behavior and causes
// threads to be made runnable immediately.
//
// #define EAGER_SIGNALLING
/* 
 * 保护队列被设计为：
 *   - 当前线程已经完成了一定量的工作，才通知线程
 *   - 采用延迟通知的方式，可以阻止/减少过度的上下文切换
 * 但是可以定义宏 EAGER_SIGNALLING 来关闭上述行为，让通知立即发出
 */

extern ClassAllocator<Event> eventAllocator;

void
ProtectedQueue::enqueue(Event *e, bool fast_signal)
{
  ink_assert(!e->in_the_prot_queue && !e->in_the_priority_queue);
  /*
   * doEThread 为任务发起方所在线程，runEThread 为任务执行方所在线程，通过 event 对象传递
   * 将任务描述（状态机）。
   * 通常由 doEThread 创建一个 Event 对象，但是该对象的成员 ethread 成员为 NULL，
   * 然后，再通过 eventProcessor.schedule_*() 为该 Event 分配一个 runEThread，然后调用此方法。
   * 由于 doEThread  可能跟 runEThread 在同一个线程池中，因此 doEThread 可能与 runEThread 相同。
   * 所以，这里需要考虑 e->ethread 可能会等于 doEThread 的情况，而 doEThread 则为 this_ethread().
   */
  EThread *e_ethread   = e->ethread;
  e->in_the_prot_queue = 1;
  // ink_atomiclist_push 执行原子操作将 e 压入到 al 的头部，返回值为压入之前的头部
  // 因此 was_empty 为 true 表示压入之前 al 是空的
  bool was_empty       = (ink_atomiclist_push(&al, e) == nullptr);

  if (was_empty) {
    // 如果保护队列压入新 event 之前为空
    // inserting_thread 为发起插入操作的队列
    // 例如：从 DEDICATED ACCEPT THREAD 插入 event 到 ET_NET，
    //     那么 inserting_thread 就是 DEDICATED ACCEPT THREAD
    EThread *inserting_thread = this_ethread();
    // queue e->ethread in the list of threads to be signalled
    // inserting_thread == 0 means it is not a regular EThread
    /*
     * 如果 doEThread 与 runEThread 为同一的 EThread，那么这里不需要进行特殊处理，
     *     此时，发起插入操作的 EThread 就是将要处理该 Event 的 EThread，这叫做内部插入。
     * 如果 doEThread 与 runEThread 不同，才需要进行下面的处理流程，
     *     此时，发起插入操作的 EThread 不是将要处理该 Event 的 EThread，简单说就是从线程外部插入
     *     这个 event 是由当前 EThread(inserting_thread / doEThread) 创建，要插入到另外一个 EThread(e_ethread / runEThread)
     *     调用本方法的方式必定是通过 e_ethread->ExternalEventQueue.enqueue() 的方式
     */
    if (inserting_thread != e_ethread) {
      e_ethread->tail_cb->signalActivity();
    }
  }
}

// 当通知被放入 signal 队列时，会在 EThread::execute() 的 REGULAR 模式中进行
// 判断，如果发现 signal 队列有元素，就会调用 flush_signals(this) 进行通知.
void
flush_signals(EThread *thr)
{
  ink_assert(this_ethread() == thr);
  int n = thr->n_ethreads_to_be_signalled;
  if (n > eventProcessor.n_ethreads) {
    n = eventProcessor.n_ethreads; // MAX
  }
  int i;

  for (i = 0; i < n; i++) {
    if (thr->ethreads_to_be_signalled[i]) {
      // 直接向该管道写
      thr->ethreads_to_be_signalled[i]->tail_cb->signalActivity();
      thr->ethreads_to_be_signalled[i] = nullptr;
    }
  }
  // 队列长度清零
  // 由于队列长度在达到最大值时会转换为映射表，而且没有逆向转换的逻辑，
  // 因此每次要完全处理表内所有的元素
  thr->n_ethreads_to_be_signalled = 0;
}

void
ProtectedQueue::dequeue_timed(ink_hrtime cur_time, ink_hrtime timeout, bool sleep)
{
  (void)cur_time;
  if (sleep) {
    this->wait(timeout);
  }
  this->dequeue_external();
}

void
ProtectedQueue::dequeue_external()
{
  Event *e = (Event *)ink_atomiclist_popall(&al);
  // invert the list, to preserve order
  SLL<Event, Event::Link_link> l, t;
  t.head = e;
  while ((e = t.pop())) {
    l.push(e);
  }
  // insert into localQueue
  while ((e = l.pop())) {
    if (!e->cancelled) {
      localQueue.enqueue(e);
    } else {
      e->mutex = nullptr;
      eventAllocator.free(e);
    }
  }
}

void
ProtectedQueue::wait(ink_hrtime timeout)
{
  ink_mutex_acquire(&lock);
  if (INK_ATOMICLIST_EMPTY(al)) {
    timespec ts = ink_hrtime_to_timespec(timeout);
    ink_cond_timedwait(&might_have_data, &lock, &ts);
  }
  ink_mutex_release(&lock);
}
