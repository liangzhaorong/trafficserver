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
#include "I_Continuation.h"
#include "I_Processor.h"
#include "I_Event.h"
#include <atomic>

#ifdef TS_MAX_THREADS_IN_EACH_THREAD_TYPE
constexpr int MAX_THREADS_IN_EACH_TYPE = TS_MAX_THREADS_IN_EACH_THREAD_TYPE;
#else
constexpr int MAX_THREADS_IN_EACH_TYPE = 3072;
#endif

#ifdef TS_MAX_NUMBER_EVENT_THREADS
constexpr int MAX_EVENT_THREADS = TS_MAX_NUMBER_EVENT_THREADS;
#else
constexpr int MAX_EVENT_THREADS        = 4096;
#endif

class EThread;

/**
  Main processor for the Event System. The EventProcessor is the core
  component of the Event System. Once started, it is responsible for
  creating and managing groups of threads that execute user-defined
  tasks asynchronously at a given time or periodically.

  The EventProcessor provides a set of scheduling functions through
  which you can specify continuations to be called back by one of its
  threads. These function calls do not block. Instead they return an
  Event object and schedule the callback to the continuation passed in at
  a later or specific time, as soon as possible or at certain intervals.

  EventProcessor 负责启动 EThread，进行每一个 EThread 的初始化。

  事件系统的主线程组（eventProcessor），是事件系统的核心组成部分。
    - 启动后，它负责创建和管理线程组，线程组中的线程则周期性的或者在指定的时间执行
      用户自定义的异步任务。
    - 通过它提供的一组调度函数，你可以让一个线程来回调指定的 Continuation。这些函数调用不会阻塞。
    - 它们返回一个 Event 对象，并安排在稍后或特定时间之后尽快或以一定的时间间隔回调 Continuation。

  Singleton model:

  Every executable that imports and statically links against the
  EventSystem library is provided with a global instance of the
  EventProcessor called eventProcessor. Therefore, it is not necessary to
  create instances of the EventProcessor class because it was designed
  as a singleton. It is important to note that none of its functions
  are reentrant.

  唯一实例模式：
    - 每一个可执行的操作都是通过全局实例 eventProcessor 提供给 EThread 组
    - 没有必要创建重复的 EventProcessor 实例，因此 EventProcessor 被设计为唯一实例模式
    - 注意：EventProcessor 的任何函数/方法都是可重入的
  

  Thread Groups (Event types):

  When the EventProcessor is started, the first group of threads is spawned and it is assigned the
  special id ET_CALL. Depending on the complexity of the state machine or protocol, you may be
  interested in creating additional threads and the EventProcessor gives you the ability to create a
  single thread or an entire group of threads. In the former case, you call spawn_thread and the
  thread is independent of the thread groups and it exists as long as your continuation handle
  executes and there are events to process. In the latter, you call @c registerEventType to get an
  event type and then @c spawn_event_theads which creates the threads in the group of that
  type. Such threads require events to be scheduled on a specicif thread in the group or for the
  grouop in general using the event type. Note that between these two calls @c
  EThread::schedule_spawn can be used to set up per thread initialization.

  线程组（事件类型）：
    - 当 EventProcessor 开始时，第一组诞生的线程被分配了特定的 id：ET_CALL。
    - 根据不同的状态机或协议的复杂性，你一定需要创建额外的线程和 EventProcessor，你可以选择：
        - 通过 spawn_thread 创建一个独立的线程
            - 该线程是完全独立于线程组之外的
            - 在 continuation 执行结束之前，它都不会退出
            - 该线程有自己的事件系统
        - 通过 spawn_event_threads 创建一个线程组
            - 你会得到一个 id 或 Event Type
            - 你可以在将来通过该 id 或者 Event Type 在该组线程上调度 continuation

  Callback event codes:

  @b UNIX: For all of the scheduling functions, the callback_event
  parameter is not used. On a callback, the event code passed in to
  the continuation handler is always EVENT_IMMEDIATE.

  @b NT: The value of the event code passed in to the continuation
  handler is the value provided in the callback_event parameter.

  回调事件的代码：
    - UNIX: 
        - 在所有的调度函数中，并未使用 callback_event 参数
        - 回调时，传递给 continuation 处理函数的事件代码始终是 EVENT_IMMEDIATE
    - NT（相关代码在开源时已经被砍掉）:
        - 递给延续处理函数的事件代码的值是由callback_event参数提供。
    

  Event allocation policy:

  Events are allocated and deallocated by the EventProcessor. A state
  machine may access the returned, non-recurring event until it is
  cancelled or the callback from the event is complete. For recurring
  events, the Event may be accessed until it is cancelled. Once the event
  is complete or cancelled, it's the eventProcessor's responsibility to
  deallocate it.

  事件分配策略：
    - 事件由 EventProcessor 分配和释放
    - 状态机可以访问返回的（returned），非周期性（non-recurring）事件，直到它被取消或该事件的回调完成
    - 对于周期性事件，该事件可以被访问，直到它被取消
    - 一但事件完成或取消，EventProcessor 将释放它

*/
class EventProcessor : public Processor
{
public:
  /** Register an event type with @a name.

      This must be called to get an event type to pass to @c spawn_event_threads
      @see spawn_event_threads
   */
  EventType register_event_type(char const *name);

  /**
    Spawn an additional thread for calling back the continuation. Spawns
    a dedicated thread (EThread) that calls back the continuation passed
    in as soon as possible.

    @param cont continuation that the spawn thread will call back
      immediately.
    @return event object representing the start of the thread.

  */
  // 创建一个包含 Cont 的立即执行的事件（Event）
  // 然后创建 DEDICATED 类型的 EThread，并传入该事件（Event）
  // 然后启动 EThread，并将事件（Event）返回调用者
  // EThread 启动后会立即调用 Cont->handleEvent
  Event *spawn_thread(Continuation *cont, const char *thr_name, size_t stacksize = 0);

  /** Spawn a group of @a n_threads event dispatching threads.

      The threads run an event loop which dispatches events scheduled for a specific thread or the event type.

      @return EventType or thread id for the new group of threads (@a ev_type)

  */
  // 创建一个数量为 n_threads 的线程组
  // 线程组的 id 取 n_thread_groups，然后增加它的计数
  EventType spawn_event_threads(EventType ev_type, int n_threads, size_t stacksize = DEFAULT_STACKSIZE);

  /// Convenience overload.
  /// This registers @a name as an event type using @c registerEventType and then calls the real @c spawn_event_threads
  EventType spawn_event_threads(const char *name, int n_thread, size_t stacksize = DEFAULT_STACKSIZE);

  /**
    Schedules the continuation on a specific EThread to receive an event
    at the given timeout.  Requests the EventProcessor to schedule
    the callback to the continuation 'c' at the time specified in
    'atimeout_at'. The event is assigned to the specified EThread.
    在特定 EThread 上调度该 continuation，以在指定超时内接收事件。请求
    EventProcessor 立即调度对于此 continuation 的回调，该回调由指定的 EThread
    处理。

    @param c Continuation to be called back at the time specified in
      'atimeout_at'.
    @param atimeout_at time value at which to callback.
    @param ethread EThread on which to schedule the event.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling
      of this callback.

  */
  Event *schedule_imm(Continuation *c, EventType event_type = ET_CALL, int callback_event = EVENT_IMMEDIATE,
                      void *cookie = nullptr);
  /*
    provides the same functionality as schedule_imm and also signals the thread immediately
    - 与 schedule_imm 相似的功能，只是在最后还要向线程发送信号
    - 这个函数其实是专为网络模块设计的
        - 一个线程可能一直阻塞在 epoll_wait 上，通过引入一个 pipe 或者 eventfd，当调度一个线程执行某个 event 时，
          异步通知该线程从 epoll_wait 解放出来
        - 另外一个具有相同功能函数 EThread::schedule_imm_signal
  */
  Event *schedule_imm_signal(Continuation *c, EventType event_type = ET_CALL, int callback_event = EVENT_IMMEDIATE,
                             void *cookie = nullptr);
  /**
    Schedules the continuation on a specific thread group to receive an
    event at the given timeout. Requests the EventProcessor to schedule
    the callback to the continuation 'c' at the time specified in
    'atimeout_at'. The callback is handled by a thread in the specified
    thread group (event_type).
    请求 EventProcessor 在指定时间（绝对时间）调度对于此 continuation 的
    回调，该回调由执行的线程组（event_type）里的线程处理。

    @param c Continuation to be called back at the time specified in
      'atimeout_at'.
    @param atimeout_at Time value at which to callback.
    @param event_type thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_at(Continuation *c, ink_hrtime atimeout_at, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules the continuation on a specific thread group to receive an
    event after the specified timeout elapses. Requests the EventProcessor
    to schedule the callback to the continuation 'c' after the time
    specified in 'atimeout_in' elapses. The callback is handled by a
    thread in the specified thread group (event_type).
    请求 EventProcessor 在指定时间内（相对时间）调度对于此 continuation 的回调，
    该回调由指定线程组（event_type）里的线程处理。

    @param c Continuation to call back aftert the timeout elapses.
    @param atimeout_in amount of time after which to callback.
    @param event_type Thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_in(Continuation *c, ink_hrtime atimeout_in, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                     void *cookie = nullptr);

  /**
    Schedules the continuation on a specific thread group to receive
    an event periodically. Requests the EventProcessor to schedule the
    callback to the continuation 'c' everytime 'aperiod' elapses. The
    callback is handled by a thread in the specified thread group
    (event_type).
    请求 EventProcessor 在每隔指定的时间调度对于此 continuation 的回调，
    该回调由指定的线程组（event_type）里的线程处理.
    如果时间间隔为负数，则表示此回调为随时执行，只要有机会，EventProcessor
    就会进行调度。
      - 目前此种类型的调度只有 epoll_wait 一种

    @param c Continuation to call back everytime 'aperiod' elapses.
    @param aperiod duration of the time period between callbacks.
    @param event_type thread group id (or event type) specifying the
      group of threads on which to schedule the callback.
    @param callback_event code to be passed back to the continuation's
      handler. See the Remarks section.
    @param cookie user-defined value or pointer to be passed back in
      the Event's object cookie field.
    @return reference to an Event object representing the scheduling of
      this callback.

  */
  Event *schedule_every(Continuation *c, ink_hrtime aperiod, EventType event_type = ET_CALL, int callback_event = EVENT_INTERVAL,
                        void *cookie = nullptr);

  ////////////////////////////////////////////
  // reschedule an already scheduled event. //
  // may be called directly or called by    //
  // schedule_xxx Event member functions.   //
  // The returned value may be different    //
  // from the argument e.                   //
  ////////////////////////////////////////////
  // 这些 reschedule_* 方法已经废弃，整个 ATS 代码中没有任何调用和声明的部分
  Event *reschedule_imm(Event *e, int callback_event = EVENT_IMMEDIATE);
  Event *reschedule_at(Event *e, ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);
  Event *reschedule_in(Event *e, ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);
  Event *reschedule_every(Event *e, ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  /// Schedule an @a event on continuation @a c when a thread of type @a ev_type is spawned.
  /// The @a cookie is attached to the event instance passed to the continuation.
  /// @return The scheduled event.
  Event *schedule_spawn(Continuation *c, EventType ev_type, int event = EVENT_IMMEDIATE, void *cookie = nullptr);

  /// Schedule the function @a f to be called in a thread of type @a ev_type when it is spawned.
  Event *schedule_spawn(void (*f)(EThread *), EventType ev_type);

  /// Schedule an @a event on continuation @a c to be called when a thread is spawned by this processor.
  /// The @a cookie is attached to the event instance passed to the continuation.
  /// @return The scheduled event.
  //  Event *schedule_spawn(Continuation *c, int event, void *cookie = NULL);

  EventProcessor();
  ~EventProcessor() override;
  EventProcessor(const EventProcessor &) = delete;
  EventProcessor &operator=(const EventProcessor &) = delete;

  /**
    Initializes the EventProcessor and its associated threads. Spawns the
    specified number of threads, initializes their state information and
    sets them running. It creates the initial thread group, represented
    by the event type ET_CALL.
    初始化 EventProcessor 及其关联的线程。创建指定的线程数，同时初始化它们的
    状态信息并设置它们运行。它创建的初始线程组，用 event type 为 ET_CALL 来表示. 

    @return 0 if successful, and a negative value otherwise.

  */
  int start(int n_net_threads, size_t stacksize = DEFAULT_STACKSIZE) override;

  /**
    Stop the EventProcessor. Attempts to stop the EventProcessor and
    all of the threads in each of the thread groups.
    停止 EventProcessor。尝试停止 EventProcessor 和每个线程组中的所有线程。

  */
  void shutdown() override;

  /**
    Allocates size bytes on the event threads. This function is thread
    safe.
    从线程的私有数据区分配空间，通过原子操作来分配，所以是线程安全的。

    @param size bytes to be allocated.

  */
  /*
   * 该函数返回可分配的私有数据空间的起始偏移地址，则如何在这个位置存取数据？
   *   - 可以通过宏定义 ETHREAD_GET_PTR(thread, offset) 获得一个指针
   *   - 然后再对这个指针所在的地址进行初始化操作
   *   - 初始化时一定要保证与申请这块内存区域时相同的数据类型的尺寸
   * 可分析 UnixNetProcessor::init 中的相关过程
   *
   * 在线程私有数据区存放的数据，通常是需要一次性分配，然后就不会释放的，如：
   *   - NetHandler
   *   - PollCont
   *   - udpNetHandler
   *   - PollCont(udpNetHandler)
   *   - RecRawStat
   */
  off_t allocate(int size);

  /**
    An array of pointers to all of the EThreads handled by the
    EventProcessor. An array of pointers to all of the EThreads created
    throughout the existence of the EventProcessor instance.

  */
  // 每创建一个 REGULAR 类型的 EThread 都会保存指向其实例的指针.
  EThread *all_ethreads[MAX_EVENT_THREADS];

  /**
    An array of pointers, organized by thread group, to all of the
    EThreads handled by the EventProcessor. An array of pointers to all of
    the EThreads created throughout the existence of the EventProcessor
    instance. It is a two-dimensional array whose first dimension is the
    thread group id and the second the EThread pointers for that group.

  */
  //  EThread *eventthread[MAX_EVENT_TYPES][MAX_THREADS_IN_EACH_TYPE];

  /// Data kept for each thread group.
  /// The thread group ID is the index into an array of these and so is not stored explicitly.
  // 为每个线程组保存数据。
  // 线程组 ID 是这些数组的索引，因此不会显示存储
  struct ThreadGroupDescriptor {
    std::string _name;                               ///< Name for the thread group. 线程组的名称
    int _count                = 0;                   ///< # of threads of this type. 线程的类型
    std::atomic<int> _started = 0;                   ///< # of started threads of this type. 该类型的线程是否已经启动了
    int _next_round_robin     = 0;                   ///< Index of thread to use for events assigned to this group.分配给该组的事件的线程索引
    Que(Event, link) _spawnQueue;                    ///< Events to dispatch when thread is spawned.
    EThread *_thread[MAX_THREADS_IN_EACH_TYPE] = {}; ///< The actual threads in this group. 该组包含的实际线程
  };

  /// Storage for per group data.
  // 存储每个线程组的数据
  ThreadGroupDescriptor thread_group[MAX_EVENT_TYPES];

  /// Number of defined thread groups.
  // 定义的线程组个数
  int n_thread_groups = 0;

  /**
    Total number of threads controlled by this EventProcessor.  This is
    the count of all the EThreads spawn by this EventProcessor, excluding
    those created by spawn_thread

  */
  // 总共有多少个 REGULAR 类型的 EThread，不包含 DEDICATED 类型的 EThread.
  int n_ethreads = 0;

  /*------------------------------------------------------*\
  | Unix & non NT Interface                                |
  \*------------------------------------------------------*/

  // schedule_*() 共享的底层实现
  Event *schedule(Event *e, EventType etype, bool fast_signal = false);
  // 根据 RoundRobin 算法从指定类型（etype）的线程组里选择一个 EThread，返回其指针。
  // 通常用来分配一个 EThread，然后将事件（Event）传入
  EThread *assign_thread(EventType etype);

  // 管理 DEDICATED 类型的 EThread 专用数组
  // 每创建一个 DEDICATED 类型的 EThread，则在此数组中保存指向其实例的指针
  // 同时增加 n_dthreads 的计数
  EThread *all_dthreads[MAX_EVENT_THREADS];
  int n_dthreads       = 0; // No. of dedicated threads
  // 记录线程私有数据区的分配情况
  int thread_data_used = 0;

  /// Provide container style access to just the active threads, not the entire array.
  class active_threads_type
  {
    using iterator = EThread *const *; ///< Internal iterator type, pointer to array element.
  public:
    iterator
    begin() const
    {
      return _begin;
    }

    iterator
    end() const
    {
      return _end;
    }

  private:
    iterator _begin; ///< Start of threads.
    iterator _end;   ///< End of threads.
    /// Construct from base of the array (@a start) and the current valid count (@a n).
    active_threads_type(iterator start, int n) : _begin(start), _end(start + n) {}
    friend class EventProcessor;
  };

  // These can be used in container for loops and other range operations.
  active_threads_type
  active_ethreads() const
  {
    return {all_ethreads, n_ethreads};
  }

  active_threads_type
  active_dthreads() const
  {
    return {all_dthreads, n_dthreads};
  }

  active_threads_type
  active_group_threads(int type) const
  {
    ThreadGroupDescriptor const &group{thread_group[type]};
    return {group._thread, group._count};
  }

private:
  void initThreadState(EThread *);

  /// Used to generate a callback at the start of thread execution.
  class ThreadInit : public Continuation
  {
    typedef ThreadInit self;
    EventProcessor *_evp;

  public:
    ThreadInit(EventProcessor *evp) : _evp(evp) { SET_HANDLER(&self::init); }

    int
    init(int /* event ATS_UNUSED */, Event *ev)
    {
      _evp->initThreadState(ev->ethread);
      return 0;
    }
  };
  friend class ThreadInit;
  ThreadInit thread_initializer;

  // Lock write access to the dedicated thread vector.
  // @internal Not a @c ProxyMutex - that's a whole can of problems due to initialization ordering.
  ink_mutex dedicated_thread_spawn_mutex;
};

extern inkcoreapi class EventProcessor eventProcessor;
