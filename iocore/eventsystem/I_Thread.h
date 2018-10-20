/** @file

  Thread

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

  Thread class provides the basic functionality for threads. Typically,
  there will be additional derived classes. Having a common base class
  for all threads is useful in many cases. I discuss below the use of
  Threads in the context of Event Subsystem. Hopefully this would be
  typical of other situations.

  Thread 类为 threads 提供基本的功能。典型的，将会有其他派生类。在许多情况下，
  为所有的线程提供一个共有基类是很有用的。

  EventProcessor needs to create a bunch of threads. It declares a
  class called EThread, derived from Thread. It is the responsibility of
  the EventProcessor to create and manage all the threads needed in the
  Event Subsystem (Note: we have removed the original ThreadManager class
  which used to create and manage *all* the threads in the system). By
  monitoring, we mean checking the heartbeat of each thread and the
  number of threads in the system etc.

  A derived class should either provide the function (and arguments)
  needed by the Thread class (see start()), or should define the virtual
  function execute().

  The Thread class maintains a thread_key which registers *all*
  the threads in the system (that have been created using Thread or
  a derived class), using thread specific data calls.  Whenever, you
  call this_thread() you get a pointer to the Thread that is currently
  executing you.  Additionally, the EThread class (derived from Thread)
  maintains its own independent key. All (and only) the threads created
  in the Event Subsystem are registered with this key. Thus, whenever you
  call this_ethread() you get a pointer to EThread. If you happen to call
  this_ethread() from inside a thread which is not an EThread, you will
  get a nullptr value (since that thread will not be  registered with the
  EThread key). This will hopefully make the use of this_ethread() safer.
  Note that an event created with EThread can also call this_thread(),
  in which case, it will get a pointer to Thread (rather than to EThread).

  Thread 类包含一个 thread_key（成员 thread_data_key），所有的线程都通过线程
  特有数据相关的调用，借助 thread_key 注册到系统中（包含了所有创建出来的 Thread
  类型和其继承类的实例）。

  任何时候，只要你调用 this_thread() 你就可以得到一个指向运行当前代码的 Thread
  实例的指针。

  // 这句注释的描述的机制没有在代码中体现，可能由于代码的删减，现在已经没有了。
  Additionally, the EThread class (derived from Thread) maintains its own independent key. 
  另外，EThread 类（继承自 Thread）维护一个独属于自己的 thread_key。

  所有 EventSystem 中创建的线程都使用这个（同一个）thread_key 注册。

  // 这句注释的描述的机制没有在代码中体现，可能由于代码的删减，现在已经没有了。
  If you happen to call this_ethread() from inside a thread which is not an EThread, you will
  get a nullptr value (since that thread will not be  registered with the EThread key). 
  This will hopefully make the use of this_ethread() safer.
  如果你从一个不是 EThread 的线程中调用 this_ethread()，那么你会得到 NULL。因为这个线程没有
  使用 EThread 的 key 来注册。我们希望可以让 this_ethread() 的使用更安全。

  需要值的注意的是，在 EThread 中可以调用 this_thread()，但是，得到的是一个指向 Thread 类型
  的指针（而不是 EThread 类型）。
  注：在当前的代码中，可以通过类型转换为 EThread，在 this_ethread() 里就是这么做的。
  
 */

#pragma once

#if !defined(_I_EventSystem_h) && !defined(_P_EventSystem_h)
#error "include I_EventSystem.h or P_EventSystem.h"
#endif

#include <functional>

#include "tscore/ink_platform.h"
#include "tscore/ink_thread.h"
#include "I_ProxyAllocator.h"

class ProxyMutex;

constexpr int MAX_THREAD_NAME_LENGTH = 16;

/// The signature of a function to be called by a thread.
using ThreadFunction = std::function<void()>;

/**
  Base class for the threads in the Event System. Thread is the base
  class for all the thread classes in the Event System. Objects of the
  Thread class represent spawned or running threads and provide minimal
  information for its derived classes. Thread objects have a reference
  to a ProxyMutex, that is used for atomic operations internally, and
  an ink_thread member that is used to identify the thread in the system.

  You should not create an object of the Thread class, they are typically
  instantiated after some thread startup mechanism exposed by a processor,
  but even then you would probably deal with processor functions and
  not the Thread object itself.

*/
class Thread
{
public:
  /*-------------------------------------------*\
  | Common Interface                            |
  \*-------------------------------------------*/

  /**
    System-wide thread identifier. The thread identifier is represented
    by the platform independent type ink_thread and it is the system-wide
    value assigned to each thread. It is exposed as a convenience for
    processors and you should not modify it directly.

  */
  ink_thread tid = 0;

  /**
    Thread lock to ensure atomic operations. The thread lock available
    to derived classes to ensure atomic operations and protect critical
    regions. Do not modify this member directly.

  */
  Ptr<ProxyMutex> mutex;

  // PRIVATE
  Thread();
  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;
  virtual ~Thread();

  void set_specific();

  static ink_hrtime cur_time;
  inkcoreapi static ink_thread_key thread_data_key;

  // For THREAD_ALLOC
  ProxyAllocator eventAllocator;
  ProxyAllocator netVCAllocator;
  ProxyAllocator sslNetVCAllocator;
  ProxyAllocator http1ClientSessionAllocator;
  ProxyAllocator http2ClientSessionAllocator;
  ProxyAllocator http2StreamAllocator;
  ProxyAllocator httpServerSessionAllocator;
  ProxyAllocator hdrHeapAllocator;
  ProxyAllocator strHeapAllocator;
  ProxyAllocator cacheVConnectionAllocator;
  ProxyAllocator openDirEntryAllocator;
  ProxyAllocator ramCacheCLFUSEntryAllocator;
  ProxyAllocator ramCacheLRUEntryAllocator;
  ProxyAllocator evacuationBlockAllocator;
  ProxyAllocator ioDataAllocator;
  ProxyAllocator ioAllocator;
  ProxyAllocator ioBlockAllocator;

public:
  /** Start the underlying thread.

      The thread name is set to @a name. The stack for the thread is either @a stack or, if that is
      @c nullptr a stack of size @a stacksize is allocated and used. If @a f is present and valid it
      is called in the thread context. Otherwise the method @c execute is invoked.
  */
  void start(const char *name, void *stack, size_t stacksize, ThreadFunction const &f = ThreadFunction());

  virtual void execute() = 0;

  /** Get the current ATS high resolution time.
      This gets a cached copy of the time so it is very fast and reasonably accurate.
      The cached time is updated every time the actual operating system time is fetched which is
      at least every 10ms and generally more frequently.
      @note The cached copy shared among threads which means the cached copy is udpated
      for all threads if any thread updates it.
  */
  static ink_hrtime get_hrtime();

  /** Get the operating system high resolution time.

      Get the current time at high resolution from the operating system.  This is more expensive
      than @c get_hrtime and should be used only where very precise timing is required.

      @note This also updates the cached time.
  */
  static ink_hrtime get_hrtime_updated();
};

extern Thread *this_thread();

TS_INLINE ink_hrtime
Thread::get_hrtime()
{
  return cur_time;
}

TS_INLINE ink_hrtime
Thread::get_hrtime_updated()
{
  return cur_time = ink_get_hrtime_internal();
}
