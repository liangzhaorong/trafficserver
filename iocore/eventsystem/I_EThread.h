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

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/ink_rand.h"
#include "tscore/I_Version.h"
#include "I_Thread.h"
#include "I_PriorityEventQueue.h"
#include "I_ProtectedQueue.h"

// TODO: This would be much nicer to have "run-time" configurable (or something),
// perhaps based on proxy.config.stat_api.max_stats_allowed or other configs. XXX
#define PER_THREAD_DATA (1024 * 1024)

// This is not used by the cache anymore, it uses proxy.config.cache.mutex_retry_delay
// instead.
#define MUTEX_RETRY_DELAY HRTIME_MSECONDS(20)

struct DiskHandler;
struct EventIO;

class ServerSessionPool;
class Event;
class Continuation;

/**
 * ATS 可以创建两种类型的 EThread 线程：
 *   - DEDICATED 类型
 *       - 该类型的线程，在执行完成某个 Continuation 后，就消亡了
 *       - 换句话说，这个类型只处理/执行一个 Event，在创建这种类型的 EThread 时就要传入 Event。
 *       - 或者是处理一个独立的任务，例如 NetAccept 的 Continuation 就是通过此种类型来执行的。
 *   - REGULAR 类型
 *       - 该类型的线程，在 ATS 启动后，就一直存在着，它的线程执行函数：execute() 是一个死循环
 *       - 该类型的线程维护了一个事件池，当事件池不为空时，线程就从事件池中取出事件（Event），
 *         同时执行事件（Event）封装的 Continuation。
 *       - 当需要调度某个线程执行某个 Continuation 时，通过 EventProcessor 将一个 Continuation 
 *         封装成一个事件（Event）并将其加入到线程的事件池中。
 *       - 这个类型是 EventSystem 的核心，它会处理很多的 Event，而且可以从外部传入 Event，然后
 *         让这个 EThread 处理/执行 Event。
 * 
 * 在创建一个 EThread 实例之前就要决定它的类型，创建之后就不能更改了。
 */
enum ThreadType {
  REGULAR = 0,
  DEDICATED,
};

extern bool shutdown_event_system;

/**
  Event System specific type of thread.
  事件系统特定类型的线程。

  The EThread class is the type of thread created and managed by
  the Event System. It is one of the available interfaces for
  schedulling events in the event system (another two are the Event
  and EventProcessor classes).
  EThread 类是由 Event System 创建和管理的线程类型。它是 Event System 
  为调度事件而提供的接口之一（另两个是 Event 和 EventProcessor 类）.

  In order to handle events, each EThread object has two event
  queues, one external and one internal. The external queue is
  provided for users of the EThread (clients) to append events to
  that particular thread. Since it can be accessed by other threads
  at the same time, operations using it must proceed in an atomic
  fashion.
  为了处理事件，每个 EThread 对象都有两个事件队列，一个是外部而一个是内部。
  外部队列提供给 EThread（用户）以便用户将事件添加到特定线程。由于它可以
  被多个线程同时访问，因此使用它时必须以原子方式进行。
  
  The internal queue, in the other hand, is used exclusively by the
  EThread to process timed events within a certain time frame. These
  events are queued internally and they may come from the external
  queue as well.
  另一方面，内部队列专门用于由 EThread 处理在一定时间框架内的定时事件。
  这些事件在内部被排队，也可能包含来自外部队列的事件。

  为了处理事件，每个 EThread 实例实际上有四个事件队列：
    - 外部队列（EventQueueExternal 被声明为 EThread 的成员，是一个保护队列）
        - 让 EThread 的调用者，能够追加事件到该特定的线程
        - 由于它同时可以被其他线程访问，所以对它的操作必须保持原子性
    - 本地队列（外部队列的子队列）
        - 外部队列是满足原子操作的，可以批量导入本地队列
        - 本地队列只能被当前线程访问，不支持原子操作
    - 内部队列（EventQueue 被声明为 EThread 的成员，是一个优先级队列）
        - 在另一方面，专门用于由 EThread 处理在一定时间框架内的定时事件
        - 这些事件在内部被排队
        - 也可能包含来自外部队列的事件（本地队列内符合条件的事件会进入到内部队列）
    - 隐性队列（NegativeQueue 被声明为 execute() 函数的局部变量）
        - 由于是局部变量，不可被外部直接操作，所以称之为隐性队列
        - 通常只有 epoll_wait 的事件出现在这个队列里

  Scheduling Interface:

  There are eight schedulling functions provided by EThread and
  they are a wrapper around their counterparts in EventProcessor.

  调度界面：
  EThread 提供了 8 个调度函数（其实是 9 个，但是有一个 _signal 除了 IOCore 内部使用，
  其它地方都用不到）。
  以下方法把 Event 放入当前的 EThread 线程的外部队列：
    - schedule_imm
    - schedule_imm_signal
        - 专为网络模块设计
        - 一个线程可能一直阻塞在 epoll_wait 上，通过引入一个 pipe 或着 eventfd，当调度
          一个线程执行某个 event 时，异步通知该线程从 epoll_wait 解放出来。
    - schedule_at
    - schedule_in
    - schedule_every
  上述 5 个方法最后都会调用 schedule() 的公共部分。
  以下方法功能与上面的相同，只是它们直接把 Event 放入内部队列
    - schedule_imm_local
    - schedule_at_local
    - schedule_in_local
    - schedule_every_local
  上述 4 个方法最后都会调用 schedule_local() 的公共部分。

  @see EventProcessor
  @see Event

*/
class EThread : public Thread
{
public:
  /** Handler for tail of event loop.

      The event loop should not spin. To avoid that a tail handler is called to block for a limited time.
      This is a protocol class that defines the interface to the handler.
  */
  class LoopTailHandler
  {
  public:
    /** Called at the end of the event loop to block.
        @a timeout is the maximum length of time (in ns) to block.
    */
    virtual int waitForActivity(ink_hrtime timeout) = 0;
    /** Unblock.

        This is required to unblock (wake up) the block created by calling @a cb.
    */
    virtual void signalActivity() = 0;

    virtual ~LoopTailHandler() {}
  };

  /*-------------------------------------------------------*\
  |  Common Interface                                       |
  \*-------------------------------------------------------*/

  /**
    Schedules the continuation on this EThread to receive an event
    as soon as possible.

    Forwards to the EventProcessor the schedule of the callback to
    the continuation 'c' as soon as possible. The event is assigned
    to EThread.

    @param c Continuation to be called back as soon as possible.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return Reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_imm(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);
  Event *schedule_imm_signal(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    at the given timeout.

    Forwards the request to the EventProcessor to schedule the
    callback to the continuation 'c' at the time specified in
    'atimeout_at'. The event is assigned to this EThread.

    @param c Continuation to be called back at the time specified
      in 'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_at(Continuation *c, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    after the timeout elapses.

    Instructs the EventProcessor to schedule the callback to the
    continuation 'c' after the time specified in atimeout_in elapses.
    The event is assigned to this EThread.

    @param c Continuation to be called back after the timeout elapses.
    @param atimeout_in Amount of time after which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' in the EventProcessor
    to occur every time 'aperiod' elapses. It is scheduled on this
    EThread.

    @param c Continuation to call back everytime 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_every(Continuation *c, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    as soon as possible.

    Schedules the callback to the continuation 'c' as soon as
    possible. The event is assigned to this EThread.

    @param c Continuation to be called back as soon as possible.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_imm_local(Continuation *c, int callback_event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    at the given timeout.

    Schedules the callback to the continuation 'c' at the time
    specified in 'atimeout_at'. The event is assigned to this
    EThread.

    @param c Continuation to be called back at the time specified
      in 'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_at_local(Continuation *c, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    after the timeout elapses.

    Schedules the callback to the continuation 'c' after the time
    specified in atimeout_in elapses. The event is assigned to this
    EThread.

    @param c Continuation to be called back after the timeout elapses.
    @param atimeout_in Amount of time after which to callback.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_in_local(Continuation *c, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /**
    Schedules the continuation on this EThread to receive an event
    periodically.

    Schedules the callback to the continuation 'c' to occur every
    time 'aperiod' elapses. It is scheduled on this EThread.

    @param c Continuation to call back everytime 'aperiod' elapses.
    @param aperiod Duration of the time period between callbacks.
    @param callback_event Event code to be passed back to the
      continuation's handler. See the Remarks section in the
      EventProcessor class.
    @param cookie User-defined value or pointer to be passed back
      in the Event's object cookie field.
    @return A reference to an Event object representing the schedulling
      of this callback.

  */
  Event *schedule_every_local(Continuation *c, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL, void *cookie = nullptr);

  /** Schedule an event called once when the thread is spawned.

      This is useful only for regular threads and if called before @c Thread::start. The event will be
      called first before the event loop.

      @Note This will override the event for a dedicate thread so that this is called instead of the
      event passed to the constructor.
  */
  Event *schedule_spawn(Continuation *c, int ev = EVENT_IMMEDIATE, void *cookie = nullptr);

  // Set the tail handler.
  void set_tail_handler(LoopTailHandler *handler);

  /* private */

  Event *schedule_local(Event *e);

  InkRand generator = static_cast<uint64_t>(Thread::get_hrtime_updated() ^ reinterpret_cast<uintptr_t>(this));

  /*-------------------------------------------------------*\
  |  UNIX Interface                                         |
  \*-------------------------------------------------------*/

  // 基本构造函数，初始化 thread_private，同时成员 tt 初始化为 REGULAR，
  // 成员 id 初始化为 NO_ETHREAD_ID
  EThread();
  // 增加对 ethreads_to_be_signalled 的初始化，同时成员 tt 初始化为 att 的值，
  // 同时成员 tt 初始化为 att 的值，成员 id 初始化为 anid 的值
  EThread(ThreadType att, int anid);
  // 专用于 DEDICATED 类型的构造函数，成员 tt 初始化为 att 的值，oneevent 初始化指向 e
  EThread(ThreadType att, Event *e);
  EThread(const EThread &) = delete;
  EThread &operator=(const EThread &) = delete;
  // 析构函数，刷新 signal，并释放 ethreads_to_be_signalled
  ~EThread() override;

  Event *schedule(Event *e, bool fast_signal = false);

  // 线程内部数据，用于保存如：统计系统的数组
  /** Block of memory to allocate thread specific data e.g. stat system arrays. */
  char thread_private[PER_THREAD_DATA];

  // 连接到 Disk Processor 以及与之配合的 AIO 队列
  /** Private Data for the Disk Processor. */
  DiskHandler *diskHandler = nullptr;

  /** Private Data for AIO. */
  Que(Continuation, link) aio_ops;

  // 外部事件队列：保护队列
  ProtectedQueue EventQueueExternal;
  // 内部事件队列：优先级队列
  PriorityEventQueue EventQueue;

  // 当 schedule 操作时，如果设置 signal 为 true 则需要触发信号通知该 Event 的 EThread
  // 但是当无法获得目标 EThread 的锁，就不能立即发送信号，因此只能先将该线程挂在这里
  // 在空闲时刻进行通知。
  // 这个指针数组的长度不会超过线程总数，当达到线程总数的时候，会转换为直接映射表。
  EThread **ethreads_to_be_signalled = nullptr;
  int n_ethreads_to_be_signalled     = 0;

  static constexpr int NO_ETHREAD_ID = -1;
  // 从 0 开始的线程 id，在 EventProcessor::start 中 new EThread 时设置
  int id                             = NO_ETHREAD_ID;
  // 事件类型，如：ET_NET, ET_SSL 等，通常用于设置该 EThread 仅处理指定类型的事件
  // 该值仅由 is_event_type 读取，set_event_type 写入
  unsigned int event_types           = 0;
  bool is_event_type(EventType et);
  void set_event_type(EventType et);

  // Private Interface

  // 事件处理机主函数
  void execute() override;
  void execute_regular();
  void process_queue(Que(Event, link) * NegativeQueue, int *ev_count, int *nq_count);
  // 处理事件（Event），回调 Cont->handleEvent
  void process_event(Event *e, int calling_code);
  // 释放事件（Event），回收资源
  void free_event(Event *e);
  LoopTailHandler *tail_cb = &DEFAULT_TAIL_HANDLER;

#if HAVE_EVENTFD
  int evfd = ts::NO_FD;
#else
  int evpipe[2];
#endif
  EventIO *ep = nullptr;

  // EThread 类型：REGULAR, DEDICATED
  ThreadType tt = REGULAR;
  /** Initial event to call, before any scheduling.
      在任何调度之前调用的初始事件。

      For dedicated threads this is the only event called.
      For regular threads this is called first before the event loop starts.
      @internal For regular threads this is used by the EventProcessor to get called back after
      the thread starts but before any other events can be dispatched to provide initializations
      needed for the thread.
      对于 dedicated 线程，这是唯一被调用的事件。
      对于 regular 线程，则在事件循环开始前首先调用它。
      @internal 对于 regular 线程，EventProcessor 使用它在线程启动后但在调度任何其他 event 前调用，
      以提供线程所需的初始化。
  */
  Event *start_event = nullptr;

  ServerSessionPool *server_session_pool = nullptr;

  /** Default handler used until it is overridden.

      This uses the cond var wait in @a ExternalQueue.
  */
  class DefaultTailHandler : public LoopTailHandler
  {
    DefaultTailHandler(ProtectedQueue &q) : _q(q) {}

    int
    waitForActivity(ink_hrtime timeout) override
    {
      _q.wait(Thread::get_hrtime() + timeout);
      return 0;
    }
    void
    signalActivity() override
    {
      _q.signal();
    }

    ProtectedQueue &_q;

    friend class EThread;
  } DEFAULT_TAIL_HANDLER = EventQueueExternal;

  /// Statistics data for event dispatching.
  struct EventMetrics {
    /// Time the loop was active, not including wait time but including event dispatch time.
    struct LoopTimes {
      ink_hrtime _start; ///< The time of the first loop for this sample. Used to mark valid entries.
      ink_hrtime _min;   ///< Shortest loop time.
      ink_hrtime _max;   ///< Longest loop time.
      LoopTimes() : _start(0), _min(INT64_MAX), _max(0) {}
    } _loop_time;

    struct Events {
      int _min;
      int _max;
      int _total;
      Events() : _min(INT_MAX), _max(0), _total(0) {}
    } _events;

    int _count; ///< # of times the loop executed.
    int _wait;  ///< # of timed wait for events

    /// Add @a that to @a this data.
    /// This embodies the custom logic per member concerning whether each is a sum, min, or max.
    EventMetrics &operator+=(EventMetrics const &that);

    EventMetrics() : _count(0), _wait(0) {}
  };

  /** The number of metric blocks kept.
      This is a circular buffer, with one block per second. We have a bit more than the required 1000
      to provide sufficient slop for cross thread reading of the data (as only the current metric block
      is being updated).
  */
  static int const N_EVENT_METRICS = 1024;

  volatile EventMetrics *current_metric = nullptr; ///< The current element of @a metrics
  EventMetrics metrics[N_EVENT_METRICS];

  /** The various stats provided to the administrator.
      THE ORDER IS VERY SENSITIVE.
      More than one part of the code depends on this exact order. Be careful and thorough when changing.
  */
  enum STAT_ID {
    STAT_LOOP_COUNT,      ///< # of event loops executed.
    STAT_LOOP_EVENTS,     ///< # of events
    STAT_LOOP_EVENTS_MIN, ///< min # of events dispatched in a loop
    STAT_LOOP_EVENTS_MAX, ///< max # of events dispatched in a loop
    STAT_LOOP_WAIT,       ///< # of loops that did a conditional wait.
    STAT_LOOP_TIME_MIN,   ///< Shortest time spent in loop.
    STAT_LOOP_TIME_MAX,   ///< Longest time spent in loop.
    N_EVENT_STATS         ///< NOT A VALID STAT INDEX - # of different stat types.
  };

  static char const *const STAT_NAME[N_EVENT_STATS];

  /** The number of time scales used in the event statistics.
      Currently these are 10s, 100s, 1000s.
  */
  static int const N_EVENT_TIMESCALES = 3;
  /// # of samples for each time scale.
  static int const SAMPLE_COUNT[N_EVENT_TIMESCALES];

  /// Process the last 1000s of data and write out the summaries to @a summary.
  void summarize_stats(EventMetrics summary[N_EVENT_TIMESCALES]);
  /// Back up the metric pointer, wrapping as needed.
  EventMetrics *
  prev(EventMetrics volatile *current)
  {
    return const_cast<EventMetrics *>(--current < metrics ? &metrics[N_EVENT_METRICS - 1] : current); // cast to remove volatile
  }
  /// Advance the metric pointer, wrapping as needed.
  EventMetrics *
  next(EventMetrics volatile *current)
  {
    return const_cast<EventMetrics *>(++current > &metrics[N_EVENT_METRICS - 1] ? metrics : current); // cast to remove volatile
  }
};

/**
  This is used so that we dont use up operator new(size_t, void *)
  which users might want to define for themselves.

*/
class ink_dummy_for_new
{
};

inline void *
operator new(size_t, ink_dummy_for_new *p)
{
  return (void *)p;
}
// 该宏定义用来返回给定线程内，线程私有数据中特定 offset 位置为起点的指针
#define ETHREAD_GET_PTR(thread, offset) ((void *)((char *)(thread) + (offset)))

// 用于获取当前 EThread 实例的指针
extern EThread *this_ethread();

extern int thread_max_heartbeat_mseconds;
