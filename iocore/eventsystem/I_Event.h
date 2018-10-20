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
#include "I_Action.h"

//
//  Defines
//

#define MAX_EVENTS_PER_THREAD 100000

// Events

#define EVENT_NONE CONTINUATION_EVENT_NONE // 0
#define EVENT_IMMEDIATE 1
#define EVENT_INTERVAL 2
#define EVENT_ERROR 3
#define EVENT_CALL 4 // used internally in state machines
#define EVENT_POLL 5 // negative event; activated on poll or epoll

// Event callback return functions

#define EVENT_DONE CONTINUATION_DONE // 0
#define EVENT_CONT CONTINUATION_CONT // 1
#define EVENT_RETURN 5
#define EVENT_RESTART 6
#define EVENT_RESTART_DELAYED 7

// Event numbers block allocation
// ** ALL NEW EVENT TYPES SHOULD BE ALLOCATED FROM BLOCKS LISTED HERE! **

#define VC_EVENT_EVENTS_START 100
#define NET_EVENT_EVENTS_START 200
#define DISK_EVENT_EVENTS_START 300
#define HOSTDB_EVENT_EVENTS_START 500
#define DNS_EVENT_EVENTS_START 600
#define CONFIG_EVENT_EVENTS_START 800
#define LOG_EVENT_EVENTS_START 900
#define REFCOUNT_CACHE_EVENT_EVENTS_START 1000
#define CACHE_EVENT_EVENTS_START 1100
#define CACHE_DIRECTORY_EVENT_EVENTS_START 1200
#define CACHE_DB_EVENT_EVENTS_START 1300
#define HTTP_NET_CONNECTION_EVENT_EVENTS_START 1400
#define HTTP_NET_VCONNECTION_EVENT_EVENTS_START 1500
#define GC_EVENT_EVENTS_START 1600
#define TRANSFORM_EVENTS_START 2000
#define STAT_PAGES_EVENTS_START 2100
#define HTTP_SESSION_EVENTS_START 2200
#define HTTP2_SESSION_EVENTS_START 2250
#define HTTP_TUNNEL_EVENTS_START 2300
#define HTTP_SCH_UPDATE_EVENTS_START 2400
#define NT_ASYNC_CONNECT_EVENT_EVENTS_START 3000
#define NT_ASYNC_IO_EVENT_EVENTS_START 3100
#define RAFT_EVENT_EVENTS_START 3200
#define SIMPLE_EVENT_EVENTS_START 3300
#define UPDATE_EVENT_EVENTS_START 3500
#define LOG_COLLATION_EVENT_EVENTS_START 3800
#define AIO_EVENT_EVENTS_START 3900
#define BLOCK_CACHE_EVENT_EVENTS_START 4000
#define UTILS_EVENT_EVENTS_START 5000
#define INK_API_EVENT_EVENTS_START 60000
#define SRV_EVENT_EVENTS_START 62000
#define REMAP_EVENT_EVENTS_START 63000

// define misc events here
#define ONE_WAY_TUNNEL_EVENT_PEER_CLOSE (SIMPLE_EVENT_EVENTS_START + 1)

typedef int EventType;
const int ET_CALL         = 0;
const int MAX_EVENT_TYPES = 8; // conservative, these are dynamically allocated

class EThread;

/**
  A type of Action returned by the EventProcessor. The Event class
  is the type of Action returned by the EventProcessor as a result
  of scheduling an operation. Unlike asynchronous operations
  represented by actions, events never call reentrantly.
  
  Event 类继承自 Action 类，它是 EventProcessor 返回的专用 Action 类型，
  它作为调度操作的结果由 EventProcessor 返回。

  不同于 Action 的异步操作，Event 是不可重入的。
    - EventProcessor 总是返回 Event 对象给状态机，
    - 然后，状态机才会收到回到。
    - 不会像 Action 类的返回值，可能存在同步回调状态机的情形。

  Besides being able to cancel an event (because it is an action),
  you can also reschedule it once received.

  除了能取消事件（因为它是一个动作），你也可以在收到它的回调之后重新
  对它进行调度。

  <b>Remarks</b>

  When rescheduling an event through any of the Event class
  scheduling functions, state machines must not make these calls
  in other thread other than the one that called them back. They
  also must have acquired the continuation's lock before calling
  any of the scheduling functions.

  The rules for canceling an event are the same as those for
  actions:
  取消 Event 的规则与 Action 的规则是一样的：

  The canceler of an event must be the state machine that will be
  called back by the task and that state machine's lock must be
  held while calling cancel. Any reference to that event object
  (ie. pointer) held by the state machine must not be used after
  the cancellation.
    - Event 的取消者必须是该任务将要回调的状态机，同时在取消的过程中
      需要持有状态机的锁
    - 任何在该状态机持有的对于该 Event 对象（如：指针）的引用，在取消
      操作之后都不得继续使用.

  Event Codes:

  At the completion of an event, state machines use the event code
  passed in through the Continuation's handler function to distinguish
  the type of event and handle the data parameter accordingly. State
  machine implementers should be careful when defining the event
  codes since they can impact on other state machines presents. For
  this reason, this numbers are usually allocated from a common
  pool.
    - 在事件完成后，状态机 SM 使用 Continuation 处理函数（Cont->handlerEvent）
      传递进来的 Event Codes 来区分 Event 类型和处理相应的数据参数。
    - 定义 Event Codes 时，状态机的实现者应该小心处理，因为它们对其他状态机
      产生影响。
    - 由于这个原因，Event Codes 通常统一进行分配。
  通常在调用 Cont->handleEvent 时传递的 Event Code 有如下几种：
    #define EVENT_NONE CONTINUATION_EVENT_NONE // 0
    #define EVENT_IMMEDIATE 1
    #define EVENT_INTERVAL 2
    #define EVENT_ERROR 3
    #define EVENT_CALL 4 // used internally in state machines
    #define EVENT_POLL 5 // negative event; activated on poll or epoll
  通常 Cont->handleEvent 也会返回一个 Event Callback Code：
    #define EVENT_DONE CONTINUATION_DONE // 0
    #define EVENT_CONT CONTINUATION_CONT // 1
    #define EVENT_RETURN 5
    #define EVENT_RESTART 6
    #define EVENT_RESTART_DELAYED 7
  PS: 但是在 EThread::execute() 中没有对 Cont->handleEvent 的返回值进行判断
  EVENT_DONE 通常表示该 Event 已经成功完成了回调操作，该 Event 接下来应该被释放
  EVENT_CONT 通常表示该 Event 没有完成回调操作，还需要保留以进行下一次回调的尝试

  Time values:

  The scheduling functions use a time parameter typed as ink_hrtime
  for specifying the timeouts or periods. This is a nanosecond value
  supported by libts and you should use the time functions and
  macros defined in ink_hrtime.h.

  任务调度函数使用了一个类型为 ink_hrtime 用来指定超时或周期的时间参数。
  这是由 libts 库支持的纳秒值，你应该使用在 ink_hrtime.h 中定义的时间
  函数和宏。

  The difference between the timeout specified for schedule_at and
  schedule_in is that in the former it is an absolute value of time
  that is expected to be in the future where in the latter it is
  an amount of time to add to the current time (obtained with
  ink_get_hrtime).

  超时参数对于 schedule_at 和 schudule_in 之间的差别在于：
    - 在前者，它是绝对时间，未来的某个预定的时刻
    - 在后者，它是相对于当前时间（通过 ink_get_hrtime 得到）的一个量

*/

/**
 * 1. 使用 Event
 * 1.1 创建一个 Event 实例，有两种方式
 *   - 全局分配
 *       - Event *e = ::eventAllocator.alloc();
 *       - 默认情况都是通过全局方式分配的，因为分配内存时还不确认要交给哪一个线程来处理。
 *       - 构造函数初始化 globally_allocated(true)
 *       - 这样就需要全局锁
 *   - 本地分配
 *       - Event *e = EVENT_ALLOC(eventAllocator, this);
 *       - 如果预先知道这个 Event 一定会交给当前线程来处理，那么就采用本地分配的方法
 *       - 调用 EThread::schedule_*_local() 方法时，会修改 globally_allocated = false
 *       - 不会影响全局锁，效率更高
 * 1.2 放入 EventSystem
 *   - 根据轮询规则选择下一个线程，然后将 Event 放入选择的线程
 *       - eventProcessor.schedule(e->init(cont, timeout, period));
 *       - EThread::schedule(e->init(cont, timeout, period));
 *   - 放入当前线程
 *       - e->schedule_*();
 *       - this_ethread()->schedule_*_local(e);
 *           - 只能在 e->ethread == this_ethread 的时候使用
 * 1.3 释放 Event
 *   - 全局分配
 *       - eventAllocator.free(e);
 *       - ::eventAllocator.free(e);
 *       - 在 ATS 的代码中可看到上面的两种写法（oknet 认为两种都是一个意思，因为只有一个全局的 eventAllocator）
 *   - 自动判断
 *       - EVENT_FREE(e, eventAllocator, t);
 *       - 根据 e->globally_allocated 来判断
 * 1.4 重新调度 Event
 *   状态机在收到来自 EThread 的回调后，void *data 指向触发此次回调的 Event 对象。简单的进行类型转换后，可以调用
 * e->schedule_*() 将此 Event 重新放入当前线程。在重新调度后，Event 的类型将会被 schedule_*() 方法重新设置.
 */
class Event : public Action
{
public:
  ///////////////////////////////////////////////////////////
  // Common Interface                                      //
  ///////////////////////////////////////////////////////////

  /**
   * Event::schedule_*()
   *   - 如果事件（Event）已经存在于 EThread 的内部队列，则先从队列中删除
   *   - 然后直接向 EThread 的本地队列添加事件（Event）
   *   - 因此此方法只能向当前 EThread 事件池添加事件（Event），不能跨线程添加
   * 当通过任何 Event 类的调度函数重新调度一个事件时，状态机（SM）不可以在除了
   * 回调它的线程以外的线程中调用这些调度函数（即状态机必须在回调它的线程里调用
   * 重新调度的函数），而且必须在调用之前获得该 Continuation 的锁。
   */

  /**
   * ATS 中的事件（Event），被设计为以下四种类型：
   *   - 立即执行
   *       - timeout_at = 0, period = 0
   *       - 通过 schedule_imm 设置 callback_event = EVENT_IMMEDIATE
   *   - 绝对定时执行
   *       - timeout_at > 0, period = 0
   *       - 通过 schedule_at 设置 callback_event = EVENT_INTERVAL，即在 xx 时候执行
   *   - 相对定时执行
   *       - timeout_in > 0, period = 0
   *       - 通过 schedule_in 设置 callback_event = EVENT_INTERVAL, 即在 xx 秒内执行
   *   - 定期/周期执行
   *       - timeout_at = period > 0
   *       - 通过 schedule_every 设置 callback_event = EVENT_INTERVAL
   *
   * 另外针对隐性队列，还有一种特殊类型：
   *   - 随时执行
   *       - timeout_at = period < 0
   *       - 通过 schedule_every 设置 callback_event = EVENT_INTERVAL
   *       - 调用 Cont->handler 时会固定传送 EVENT_POLL 事件
   *       - 对于 TCP 连接，此种类型的事件（Event）由 NetHandler::startNetEvent 添加
   *           - Cont->handler 对于 TCP 事件为 NetHandler::mainNetEvent()
   */

  /**
     Reschedules this event immediately. Instructs the event object
     to reschedule itself as soon as possible in the EventProcessor.

     立即重新调度该事件。指示事件对象在 EventProcessor 中尽快重新
     调度它自己.

     @param callback_event Event code to return at the completion
      of this event. See the Remarks section.

  */
  void schedule_imm(int callback_event = EVENT_IMMEDIATE);

  /**
     Reschedules this event to callback at time 'atimeout_at'.
     Instructs the event object to reschedule itself at the time
     specified in atimeout_at on the EventProcessor.

     @param atimeout_at Time at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_at(ink_hrtime atimeout_at, int callback_event = EVENT_INTERVAL);

  /**
     Reschedules this event to callback at time 'atimeout_at'.
     Instructs the event object to reschedule itself at the time
     specified in atimeout_at on the EventProcessor.

     @param atimeout_in Time at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_in(ink_hrtime atimeout_in, int callback_event = EVENT_INTERVAL);

  /**
     Reschedules this event to callback every 'aperiod'. Instructs
     the event object to reschedule itself to callback every 'aperiod'
     from now.

     @param aperiod Time period at which to callcallback. See the Remarks section.
     @param callback_event Event code to return at the completion of this event. See the Remarks section.

  */
  void schedule_every(ink_hrtime aperiod, int callback_event = EVENT_INTERVAL);

  // inherited from Action::cancel
  // virtual void cancel(Continuation * c = nullptr);

  void free();

  /**
   * 处理此 Event 的 ethread 指针，在 ethread 处理此 Event 之前填充（就是在 schedule 时）。
   * 当一个 Event 由一个 EThread 管理后，就无法再转交给其它 EThread 管理。
   */
  EThread *ethread = nullptr;

  // 状态及标志位
  unsigned int in_the_prot_queue : 1;
  unsigned int in_the_priority_queue : 1;
  unsigned int immediate : 1;
  unsigned int globally_allocated : 1;
  unsigned int in_heap : 4;
  // 向 Cont->handler 传递的事件（Event）类型
  int callback_event = 0;

  // 组合构成四种事件（Event）类型
  ink_hrtime timeout_at = 0;
  ink_hrtime period     = 0;

  /**
    This field can be set when an event is created. It is returned
    as part of the Event structure to the continuation when handleEvent
    is called.

    创建事件时可以设置该字段。当调用 handleEvent 时，它将作为 Event 结构体
    的一部分返回给 continuation（即在回调 Cont->handle 时作为数据（Data）传递）。
  */
  void *cookie = nullptr;

  // Private

  Event();

  /**
   * - 初始化一个 Event
   * - 通常用来准备一个 Event，然后选择一个新的 EThread 线程来处理这个事件时使用
   * - 接下来通常会调用 ETread::schedule() 方法
   */
  Event *init(Continuation *c, ink_hrtime atimeout_at = 0, ink_hrtime aperiod = 0);

#ifdef ENABLE_TIME_TRACE
  ink_hrtime start_time;
#endif

  // noncopyable: prevent unauthorized copies (Not implemented)
  Event(const Event &) = delete;
  Event &operator=(const Event &) = delete;

private:
  void *operator new(size_t size); // use the fast allocators

public:
  LINK(Event, link);

  /*-------------------------------------------------------*\
  | UNIX/non-NT Interface                                   |
  \*-------------------------------------------------------*/

#ifdef ONLY_USED_FOR_FIB_AND_BIN_HEAP
  void *node_pointer;
  void
  set_node_pointer(void *x)
  {
    node_pointer = x;
  }
  void *
  get_node_pointer()
  {
    return node_pointer;
  }
#endif

#if defined(__GNUC__)
  ~Event() override {}
#endif
};

//
// Event Allocator
//
extern ClassAllocator<Event> eventAllocator;

// Event 的内存分配不对空间进行 bzero() 操作，因此在 Event::init() 方法中会初始化所有必要的值
#define EVENT_ALLOC(_a, _t) THREAD_ALLOC(_a, _t)
#define EVENT_FREE(_p, _a, _t) \
  _p->mutex = nullptr;         \
  if (_p->globally_allocated)  \
    ::_a.free(_p);             \
  else                         \
    THREAD_FREE(_p, _a, _t)
