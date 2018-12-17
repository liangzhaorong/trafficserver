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

  @section details Details

  Continuations have a handleEvent() method to invoke them. Users
  can determine the behavior of a Continuation by suppling a
  "ContinuationHandler" (member function name) which is invoked
  when events arrive. This function can be changed with the
  "setHandler" method.

  Continuations can be subclassed to add additional state and
  methods.

 */

#pragma once

#include "tscore/ink_platform.h"
#include "tscore/List.h"
#include "I_Lock.h"
#include "tscore/ContFlags.h"

class Continuation;
class ContinuationQueue;
class Processor;
class ProxyMutex;
class EThread;
class Event;

extern EThread *this_ethread();

//////////////////////////////////////////////////////////////////////////////
//
//  Constants and Type Definitions
//
//////////////////////////////////////////////////////////////////////////////

#define CONTINUATION_EVENT_NONE 0

#define CONTINUATION_DONE 0
#define CONTINUATION_CONT 1

typedef int (Continuation::*ContinuationHandler)(int event, void *data);

class force_VFPT_to_top
{
public:
  virtual ~force_VFPT_to_top() {}
};

/**
  Base class for all state machines to receive notification of
  events.
  Continuation 是所有状态机接收事件通知的基类.

  The Continuation class represents the main abstraction mechanism
  used throughout the IO Core Event System to communicate its users
  the occurrence of an event. A Continuation is a lightweight data
  structure that implements a single method with which the user is
  called back.

  Continuations are typically subclassed in order to implement
  event-driven state machines. By including additional state and
  methods, continuations can combine state with control flow, and
  they are generally used to support split-phase, event-driven
  control flow.

  Given the multithreaded nature of the Event System, every
  continuation carries a reference to a ProxyMutex object to protect
  its state and ensure atomic operations. This ProxyMutex object
  must be allocated by continuation-derived classes or by clients
  of the IO Core Event System and it is required as a parameter to
  the Continuation's class constructor.
  鉴于事件系统的多线程特性，每个 Continuation 都带有一个对 ProxyMutex 
  对象的引用，以保护其状态，并确保其原子操作。
  因此，在创建任何一个 Continuation 对象或由其派生的对象时：
  - 通常由使用 EventSystem 的客户来创建 ProxyMutex 对象
  - 然后在创建 Continuation 对象时，将 ProxyMutex 对象作为参数传递给
    Continuation 类的构造函数.

  Continuation 是一个轻型数据结构。
  它的实现只有一个用于回调的方法 int handleEvent(int, void *)
    - 该方法是面向 Processor 和 EThread 的一个接口
    - 它可以被继承，以添加额外的状态和方法.
  可以通过提供 ContinuationHandler（成员 handler 的类型）来决定 Continuation 的行为
    - 该函数在事件到达时，由 handleEvent 调用
    - 可以通过以下方法来设置/改变（头文件内定义了两个宏）：
      - SET_HANDLER(_h)
      - SET_CONTINUATION_HANDLER(_c, _h)

  Continuation，从学术上应该是叫做 Continuation 的编程模型（方法），这个技术相当古老，
  后来微软围绕这个方案，改进出了 coroutine 的编程模型（方法），一定程度上来讲 Continuation 
  是整个异步回调机制的多线程事件编程基础。对应 ATS 中，Continuation 是一个最最基础的抽象
  结构，后续的所有高级结构，如 Action Event VC 等都封装 Continuation 数据结构.
*/

class Continuation : private force_VFPT_to_top
{
public:
  /**
    The current continuation handler function.
    当前的 Continuation 处理函数

    The current handler should not be set directly. In order to
    change it, first acquire the Continuation's lock and then use
    the SET_HANDLER macro which takes care of the type casting
    issues.
    在对 mutex 成员上锁之后，使用 SET_HANDLER 宏进行设置。
    不要直接对该成员进行操作。

  */
  ContinuationHandler handler = nullptr;

#ifdef DEBUG
  const char *handler_name = nullptr;
#endif

  /**
    The Continuation's lock.
    当前 Continuation 对象的锁.

    A reference counted pointer to the Continuation's lock. This
    lock is initialized in the constructor and should not be set
    directly.
    通过构造函数完成初始化，不要直接对该成员进行操作.

    TODO:  make this private.

  */
  Ptr<ProxyMutex> mutex;

  ProxyMutex *
  getMutex() const
  {
    return mutex.get();
  }

  /**
    Link to other continuations.
    链接到其他 Continuation 的双向链表.

    A doubly-linked element to allow Lists of Continuations to be
    assembled.
    在需要创建一个队列用来保存 Continuation 对象时使用。

  */
  LINK(Continuation, link);

  /**
    Contains values for debug_override and future flags that
    needs to be thread local while this continuation is running
  */
  ContFlags control_flags;

  /**
    Receives the event code and data for an Event.
    接收来自 Event 回调的事件代码和事件数据。

    This function receives the event code and data for an event and
    forwards them to the current continuation handler. The processor
    calling back the continuation is responsible for acquiring its
    lock.  If the lock is present and not held, this method will assert.
    接收事件代码和事件数据并透传给当前的 continuation handler。
    回调 Continuation 的 Processor 负责对 Continuation 的 mutex 上锁.

    @param event Event code to be passed at callback (Processor specific).
                 由 Processor 指定，在回调时传入的事件代码
    @param data General purpose data related to the event code (Processor specific).
                由 Processor 指定，与事件代码相关的数据
    @return State machine and processor specific return code.
            状态机和 Processor 指定的返回值

  */
  TS_INLINE int
  handleEvent(int event = CONTINUATION_EVENT_NONE, void *data = nullptr)
  {
    // If there is a lock, we must be holding it on entry
    ink_release_assert(!mutex || mutex->thread_holding == this_ethread());
    return (this->*handler)(event, data);
  }

  /**
    Receives the event code and data for an Event.
    接收一个 Event 的事件代码和事件数据。

    It will attempt to get the lock for the continuation, and reschedule
    the event if the lock cannot be obtained.  If the lock can be obtained
    dispatchEvent acts like handleEvent.

    @param event Event code to be passed at callback (Processor specific).
    @param data General purpose data related to the event code (Processor specific).
    @return State machine and processor specific return code.

  */
  int dispatchEvent(int event = CONTINUATION_EVENT_NONE, void *data = nullptr);

protected:
  /**
    Constructor of the Continuation object. It should not be used
    directly. Instead create an object of a derived type.
    在 ATS 中，不会直接创建 Continuation 类型的对象，而是创建其继承类的实例。
    因此也就不存在直接与之对应的 ClassAllocator 对象。

    @param amutex Lock to be set for this Continuation.

  */
  Continuation(ProxyMutex *amutex = nullptr);
  Continuation(Ptr<ProxyMutex> &amutex);
};

/**
  Sets the Continuation's handler. The preferred mechanism for
  setting the Continuation's handler.
  设置 Continuation 的 handler.

  @param _h Pointer to the function used to callback with events.

*/
#ifdef DEBUG
#define SET_HANDLER(_h) (handler = ((ContinuationHandler)_h), handler_name = #_h)
#else
#define SET_HANDLER(_h) (handler = ((ContinuationHandler)_h))
#endif

/**
  Sets a Continuation's handler.
  设置 Continuation 对象的 handler.

  The preferred mechanism for setting the Continuation's handler.
  设置 Continuation handler 的首选机制。

  @param _c Pointer to a Continuation whose handler is being set.
            指向正在设置其 handler 成员的 Continuation 的指针
  @param _h Pointer to the function used to callback with events.
            指向将要用于回调事件的函数的指针.

*/
#ifdef DEBUG
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = ((ContinuationHandler)_h), _c->handler_name = #_h)
#else
#define SET_CONTINUATION_HANDLER(_c, _h) (_c->handler = ((ContinuationHandler)_h))
#endif

inline Continuation::Continuation(Ptr<ProxyMutex> &amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}

inline Continuation::Continuation(ProxyMutex *amutex) : mutex(amutex)
{
  // Pick up the control flags from the creating thread
  this->control_flags.set_flags(get_cont_flags().get_flags());
}
