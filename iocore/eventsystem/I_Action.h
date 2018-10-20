/** @file

  Generic interface which enables any event or async activity to be cancelled

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
#include "I_Thread.h"
#include "I_Continuation.h"

/**
  Represents an operation initiated on a Processor.
  表示在处理器上启动的操作。

  The Action class is an abstract representation of an operation
  being executed by some Processor. A reference to an Action object
  allows you to cancel an ongoing asynchronous operation before it
  completes. This means that the Continuation specified for the
  operation will not be called back.
  Action 类是由某个处理器执行的操作的抽象表示。引用 Action 对象允许你
  在一个异步操作完成之前撤销它。这意味着为此操作指定的 Continuation 将
  不会被回调。


  Actions or classes derived from Action are the typical return
  type of methods exposed by Processors in the Event System and
  throughout the IO Core libraries.
  对于 Event Ssystem 中的 Processor，还有整个 IO 核心库公开的方法/函数
  来说 Action 或其派生类是一种常见的返回类型。
  

  The canceller of an action must be the state machine that will
  be called back by the task and that state machine's lock must be
  held while calling cancel.
  action 的取消者必须是该操作将要回调的状态机，同时在取消的过程中需要
  持有状态机的锁。


  Processor implementers:
  Processor 方法实现者：

  You must ensure that no events are sent to the state machine after
  the operation has been cancelled appropriately.
  在实现一个 Processor 的方法时，必须确保：
    - 在操作被取消之后，不会有事件发送给状态机。

  Returning an Action:
  返回一个 Action：

  Processor functions that are asynchronous must return actions to
  allow the calling state machine to cancel the task before completion.
  Because some processor functions are reentrant, they can call
  back the state machine before the returning from the call that
  creates the actions. To handle this case, special values are
  returned in place of an action to indicate to the state machine
  that the action is already completed.
  Processor 方法通常是异步执行的，因此必须返回 Action，这样状态机才能在
  任务完成前随时取消该任务。
    - 此时，状态机总是先获得 Action
    - 然后才会收到该任务的回调
    - 在收到回调之前，随时可以通过 Action 取消该任务
  由于某些 Processor 的方法是可以同步执行的（可重入的），因此可能会出现先
  回调状态机，再向状态机返回 Action 的情况。此时返回 Action 是无意义的，为了
  处理这种情况，返回特殊的几个值来代替 Action 对象，以指示状态机该动作已经完成。

    - @b ACTION_RESULT_DONE The processor has completed the task
      and called the state machine back inline.
         该 Processor 已经完成了任务，并内嵌（同步）回调了状态机。
    - @b ACTION_RESULT_INLINE Not currently used.
    - @b ACTION_RESULT_IO_ERROR Not currently used.

  To make matters more complicated, it's possible if the result is
  ACTION_RESULT_DONE that state machine deallocated itself on the
  reentrant callback. Thus, state machine implementers MUST either
  use a scheme to never deallocate their machines on reentrant
  callbacks OR immediately check the returned action when creating
  an asynchronous task and if it is ACTION_RESULT_DONE neither read
  nor write any state variables. With either method, it's imperative
  that the returned action always be checked for special values and
  the value handled accordingly.
  也许会出现这样一种更复杂的问题：
    - 当结果为 ACTION_RESULT_DONE 
    - 同时，状态机在同步回调中释放了自身
  因此，状态机的实现者必须：
    - 同步回调时，不要释放自身（不容易判断出回调的类型是同步还是异步）或者，
    - 立即检查 Processor 方法返回的 Action
    - 如果该值为 ACTION_RESULT_DONE，那么就不能对状态机的任何状态变量进行读或写。
  无论使用哪种方式，都要对返回值进行检查（是否为 ACTION_RESULT_DONE），同时进行
  相应的处理。

  Allocation policy:
  分配策略：

  Actions are allocated by the Processor performing the actions.
  It is the processor's responsbility to handle deallocation once
  the action is complete or cancelled. A state machine MUST NOT
  access an action once the operation that returned the Action has
  completed or it has cancelled the Action.
  Action 的分配和释放遵循以下策略：
    - Action 由执行它的 Processor 进行分配
        - 通常 Processor 方法会创建一个 Task 状态机来异步执行某个特定任务
        - 而 Action 对象则是该 Task 状态机的一个成员对象
    - 在 Action 完成或被取消后，Processor 有责任和义务来释放它。
        - 当 Task 状态机需要回调状态机时，
            - 通过 Action 获得 mutex 并对其上锁
            - 然后检查 Action 的成员 cancelled
            - 如已经 cancelled，则销毁 Task 状态机
            - 否则回调 Action.continuation
    - 当返回的 Action 已经完成，或者状态机对一个 Action 执行了取消操作，
        - 状态机就不可以再访问该 Action

*/

/**
 * Action：
 *   - 当一个状态机通过某个 Processor 方法发起一个异步操作时，Processor 
 *     将返回一个 Action 类的指针。
 *   - 通过一个指向 Action 对象的指针，状态机可以取消正在进行中的异步操作
 *   - 在取消之后，发起该操作的状态机将不会接收到来自该异步操作的回调
 *   - 对于 Event System 中的 Processor，还有整个 IO 核心库公开的方法/函数来说
 *     Action 或其派生类是一种常见的返回类型。
 *   - Action 的取消者必须是该操作将要回调的状态机，同时在取消的过程中需要持有
 *     状态机的锁
 */
class Action
{
public:
  /**
    Contination that initiated this action.
    启动该 action 的 Continuation。

    The reference to the initiating continuation is only used to
    verify that the action is being cancelled by the correct
    continuation.  This field should not be accesed or modified
    directly by the state machine.
    启动 continuation 的引用仅用于验证该 action 是否被正确的 continuation
    取消。该字段不可以直接被状态机访问或者修改。

  */
  Continuation *continuation = nullptr;

  /**
    Reference to the Continuation's lock.

    Keeps a reference to the Continuation's lock to preserve the
    access to the cancelled field valid even when the state machine
    has been deallocated. This field should not be accesed or
    modified directly by the state machine.

  */
  Ptr<ProxyMutex> mutex;

  /**
    Internal flag used to indicate whether the action has been
    cancelled.
    内部标识，用于指示 action 是否被取消了。

    This flag is set after a call to cancel or cancel_action and
    it should not be accesed or modified directly by the state
    machine.
    该标记在调用 cancel 或者 cancel_action 后设置，且不应该被状态机
    直接访问或者修改。

  */
  int cancelled = false;

  /**
    Cancels the asynchronous operation represented by this action.
    取消该 action 表示的异步操作。

    This method is called by state machines willing to cancel an
    ongoing asynchronous operation. Classes derived from Action may
    perform additional steps before flagging this action as cancelled.
    There are certain rules that must be followed in order to cancel
    an action (see the Remarks section).

    @param c Continuation associated with this Action.

    可由继承类重写，实现继承类中对应的处理。
    作为 Action 对外部提供的唯一接口.
  */
  virtual void
  cancel(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
#ifdef DEBUG
    ink_assert(!cancelled);
    cancelled = true;
#else
    if (!cancelled) {
      cancelled = true;
    }
#endif
  }

  /**
    Cancels the asynchronous operation represented by this action.

    This method is called by state machines willing to cancel an
    ongoing asynchronous operation. There are certain rules that
    must be followed in order to cancel an action (see the Remarks
    section).

    @param c Continuation associated with this Action.

    此方法总是直接对 Action 基类设置取消操作，跳过继承类的取消流程。
    在 ATS 代码内，此方法为 Event 对象专用。
  */
  void
  cancel_action(Continuation *c = nullptr)
  {
    ink_assert(!c || c == continuation);
#ifdef DEBUG
    ink_assert(!cancelled);
    cancelled = true;
#else
    if (!cancelled) {
      cancelled = true;
    }
#endif
  }

  /**
   * 重载赋值(=)操作
   * 用于初始化 Action
   *   acont 为操作完成时回调的状态机
   *   mutex 为上述状态机的锁，采用 Ptr<> 自动指针管理
   */
  Continuation *
  operator=(Continuation *acont)
  {
    continuation = acont;
    if (acont) {
      mutex = acont->mutex;
    } else {
      mutex = nullptr;
    }
    return acont;
  }

  /**
    Constructor of the Action object. Processor implementers are
    responsible for associating this action with the proper
    Continuation.

  */
  Action() {}
  virtual ~Action() {}
};

#define ACTION_RESULT_NONE MAKE_ACTION_RESULT(0)
#define ACTION_RESULT_DONE MAKE_ACTION_RESULT(1)
#define ACTION_IO_ERROR MAKE_ACTION_RESULT(2)
#define ACTION_RESULT_INLINE MAKE_ACTION_RESULT(3)

// Use these classes by
// #define ACTION_RESULT_HOST_DB_OFFLINE
//   MAKE_ACTION_RESULT(ACTION_RESULT_HOST_DB_BASE + 0)

#define MAKE_ACTION_RESULT(_x) (Action *)(((uintptr_t)((_x << 1) + 1)))
