/** @file

  Basic locks for threads

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
#include "tscore/Diags.h"
#include "I_Thread.h"

#define MAX_LOCK_TIME HRTIME_MSECONDS(200)
#define THREAD_MUTEX_THREAD_HOLDING (-1024 * 1024)


/**
 * 加锁方法，实际上是：
 *   - 通过宏定义，创建了一个临时对象
 *   - 由对象的析构函数来调用解锁方法
 *   - 当临时对象离开作用域的时候会执行析构函数
 *   - 这样就实现了自动解锁
 */
 
/*------------------------------------------------------*\
|  Macros                                                |
\*------------------------------------------------------*/

/**
  Blocks until the lock to the ProxyMutex is acquired.
  阻塞型上锁，阻塞直到完成上锁.

  This macro performs a blocking call until the lock to the ProxyMutex
  is acquired. This call allocates a special object that holds the
  lock to the ProxyMutex only for the scope of the function or
  region. It is a good practice to delimit such scope explicitly
  with '&#123;' and '&#125;'.
  这个宏将阻塞，直到获得对 ProxyMutex 的锁。调用该宏，将分配一个
  特殊的对象持有对 ProxyMutex 的锁，但是该对象只存在于函数或者
  区域的作用域中。如，可以通过大括号来限定作用域。
  PS: 换句话说，就是这个特殊的对象是一个局部变量。

  @param _l Arbitrary name for the lock to use in this call
            给锁起个临时的名字
  @param _m A pointer to (or address of) a ProxyMutex object
            指向 ProxyMutex 的指针
  @param _t The current EThread executing your code.
            当前 EThread 线程

*/
#ifdef DEBUG
#define SCOPED_MUTEX_LOCK(_l, _m, _t) MutexLock _l(MakeSourceLocation(), nullptr, _m, _t)
#else
#define SCOPED_MUTEX_LOCK(_l, _m, _t) MutexLock _l(_m, _t)
#endif // DEBUG

#ifdef DEBUG
/**
  Attempts to acquire the lock to the ProxyMutex.
  非阻塞型上锁，需要在调用后进行判断，有可能没有成功上锁。

  This macro attempts to acquire the lock to the specified ProxyMutex
  object in a non-blocking manner. After using the macro you can
  see if it was successful by comparing the lock variable with true
  or false (the variable name passed in the _l parameter).
  这个宏将以非阻塞方式，尝试获得对 ProxyMutex 的锁。
  在使用此宏后，可以通过对 _l 这个锁的变量进行判断（true 或 false），
  来确认是否获得锁。

  @param _l Arbitrary name for the lock to use in this call (lock variable)
            给锁临时起个名字
  @param _m A pointer to (or address of) a ProxyMutex object
            指向 ProxyMutex 的指针
  @param _t The current EThread executing your code.
            当前 EThread 线程

*/
#define MUTEX_TRY_LOCK(_l, _m, _t) MutexTryLock _l(MakeSourceLocation(), (char *)nullptr, _m, _t)

#else // DEBUG
#define MUTEX_TRY_LOCK(_l, _m, _t) MutexTryLock _l(_m, _t)
#endif // DEBUG

/**
  Releases the lock on a ProxyMutex.
  释放锁/解锁

  This macro releases the lock on the ProxyMutex, provided it is
  currently held. The lock must have been successfully acquired
  with one of the MUTEX macros.

  @param _l Arbitrary name for the lock to use in this call (lock
    variable) It must be the same name as the one used to acquire the
    lock.

*/
#define MUTEX_RELEASE(_l) (_l).release()

// 使用如下两个宏加锁时需要显式解锁
/////////////////////////////////////
// DEPRECATED DEPRECATED DEPRECATED
#ifdef DEBUG
#define MUTEX_TAKE_TRY_LOCK(_m, _t) Mutex_trylock(MakeSourceLocation(), (char *)nullptr, _m, _t)
#else
#define MUTEX_TAKE_TRY_LOCK(_m, _t) Mutex_trylock(_m, _t)
#endif

#ifdef DEBUG
#define MUTEX_TAKE_LOCK(_m, _t) Mutex_lock(MakeSourceLocation(), (char *)nullptr, _m, _t)
#define MUTEX_TAKE_LOCK_FOR(_m, _t, _c) Mutex_lock(MakeSourceLocation(), nullptr, _m, _t)
#else
#define MUTEX_TAKE_LOCK(_m, _t) Mutex_lock(_m, _t)
#define MUTEX_TAKE_LOCK_FOR(_m, _t, _c) Mutex_lock(_m, _t)
#endif // DEBUG

#define MUTEX_UNTAKE_LOCK(_m, _t) Mutex_unlock(_m, _t)
// DEPRECATED DEPRECATED DEPRECATED
/////////////////////////////////////

class EThread;
typedef EThread *EThreadPtr;

#if DEBUG
inkcoreapi extern void lock_waiting(const SourceLocation &, const char *handler);
inkcoreapi extern void lock_holding(const SourceLocation &, const char *handler);
inkcoreapi extern void lock_taken(const SourceLocation &, const char *handler);
#endif

/**
  Lock object used in continuations and threads.
  ProxyMutex 是在 Continuation 和 Thread 中使用的互斥锁.

  The ProxyMutex class is the main synchronization object used
  throughout the Event System. It is a reference counted object
  that provides mutually exclusive access to a resource. Since the
  Event System is multithreaded by design, the ProxyMutex is required
  to protect data structures and state information that could
  otherwise be affected by the action of concurrent threads.
  在整个事件系统中（Event System），ProxyMutex 是最基本的同步对象。它是一个
  引用计数的对象，提供对资源的互斥访问保护。由于事件系统是多线程的模式，通过
  ProxyMutex 可以保护数据结构和状态信息，否则将会受到并发线程的影响。

  A ProxyMutex object has an ink_mutex member (defined in ink_mutex.h)
  which is a wrapper around the platform dependent mutex type. This
  member allows the ProxyMutex to provide the functionallity required
  by the users of the class without the burden of platform specific
  function calls.
  ProxyMutex 对象有一个 ink_mutex 类型（在 ink_mutex.h 中定义）的成员，
  它是对不同平台的互斥锁类型的封装。ProxyMutex 通过 ink_mutex 来实现
  互斥锁的功能，这样就没有特定平台的函数调用的负担。

  The ProxyMutex also has a reference to the current EThread holding
  the lock as a back pointer for verifying that it is released
  correctly.
  ProxyMutex 也有一个指针，指回到当前持有锁的 EThread，用于验证它是否
  正确的被释放。

  Acquiring/Releasing locks:

  Included with the ProxyMutex class, there are several macros that
  allow you to lock/unlock the underlying mutex object.

*/
class ProxyMutex : public RefCountObj
{
public:
  /**
    Underlying mutex object.

    The platform independent mutex for the ProxyMutex class. You
    must not modify or set it directly.

  */
  // coverity[uninit_member]
  // 内部互斥对象
  ink_mutex the_mutex;

  /**
    Backpointer to owning thread.

    This is a pointer to the thread currently holding the mutex
    lock.  You must not modify or set this value directly.

  */
  // 指向持有该锁的 ETthread
  EThreadPtr thread_holding;

  // 加锁、解锁计数器
  // 每加锁一次加 1，解锁一次减 1，但是减为 0 时才真正解锁
  int nthread_holding;

#ifdef DEBUG
  ink_hrtime hold_time;
  SourceLocation srcloc;
  const char *handler;

#ifdef MAX_LOCK_TAKEN
  int taken;
#endif // MAX_LOCK_TAKEN

#ifdef LOCK_CONTENTION_PROFILING
  int total_acquires, blocking_acquires, nonblocking_acquires, successful_nonblocking_acquires, unsuccessful_nonblocking_acquires;
  void print_lock_stats(int flag);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  // 在 ats 中不要直接调用此方法来释放资源
  void free() override;

  /**
    Constructor - use new_ProxyMutex() instead.

    The constructor of a ProxyMutex object. Initializes the state
    of the object but leaves the initialization of the mutex member
    until it is needed (through init()). Do not use this constructor,
    the preferred mechanism for creating a ProxyMutex is via the
    new_ProxyMutex function, which provides a faster allocation.

  */
  /**
   * 构造函数 - 请调用 new_ProxyMutex() 来代替。
   *
   * ProxyMutex 对象的构造函数。初始化对象的状态，但是不对 mutex 成员进行初始化，
   * 在使用之前可通过 init() 对 mutex 进行初始化。请不要使用这个构造函数，创建
   * 一个 ProxyMutex 实例的最佳方式是通过 new_ProxyMutex() 函数，同时提供了更快
   * 的内存分配策略。
   */
  ProxyMutex()
#ifdef DEBUG
    : srcloc(nullptr, nullptr, 0)
#endif
  {
    thread_holding  = nullptr;
    nthread_holding = 0;
#ifdef DEBUG
    hold_time = 0;
    handler   = nullptr;
#ifdef MAX_LOCK_TAKEN
    taken = 0;
#endif // MAX_LOCK_TAKEN
#ifdef LOCK_CONTENTION_PROFILING
    total_acquires                    = 0;
    blocking_acquires                 = 0;
    nonblocking_acquires              = 0;
    successful_nonblocking_acquires   = 0;
    unsuccessful_nonblocking_acquires = 0;
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
    // coverity[uninit_member]
  }

  /**
    Initializes the underlying mutex object.

    After constructing your ProxyMutex object, use this function
    to initialize the underlying mutex object with an optional name.

    @param name Name to identify this ProxyMutex. Its use depends
      on the given platform.

  */
  /**
   * 初始化内部 mutex 对象.
   *
   * 在 ProxyMutex 对象构造完成后，使用这个方法初始化内部 mutex 对象
   * 同时也可以对该对象赋予一个名字（某些平台可能不支持）
   */
  void
  init(const char *name = "UnnamedMutex")
  {
    ink_mutex_init(&the_mutex);
  }
};

// 专门用来为 ProxyMutex 类型的对象分配内存的 ClassAllocator
// The ClassAlocator for ProxyMutexes
extern inkcoreapi ClassAllocator<ProxyMutex> mutexAllocator;

inline bool
Mutex_trylock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t)
{
  ink_assert(t != nullptr);
  ink_assert(t == (EThread *)this_thread());
  if (m->thread_holding != t) {
    if (!ink_mutex_try_acquire(&m->the_mutex)) {
#ifdef DEBUG
      lock_waiting(m->srcloc, m->handler);
#ifdef LOCK_CONTENTION_PROFILING
      m->unsuccessful_nonblocking_acquires++;
      m->nonblocking_acquires++;
      m->total_acquires++;
      m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
      return false;
    }
    m->thread_holding = t;
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->successful_nonblocking_acquires++;
  m->nonblocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline bool
Mutex_trylock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t)
{
  return Mutex_trylock(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t);
}

inline bool
Mutex_trylock_spin(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t, int spincnt = 1)
{
  ink_assert(t != nullptr);
  if (m->thread_holding != t) {
    int locked;
    do {
      if ((locked = ink_mutex_try_acquire(&m->the_mutex))) {
        break;
      }
    } while (--spincnt);
    if (!locked) {
#ifdef DEBUG
      lock_waiting(m->srcloc, m->handler);
#ifdef LOCK_CONTENTION_PROFILING
      m->unsuccessful_nonblocking_acquires++;
      m->nonblocking_acquires++;
      m->total_acquires++;
      m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
      return false;
    }
    m->thread_holding = t;
    ink_assert(m->thread_holding);
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->successful_nonblocking_acquires++;
  m->nonblocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline bool
Mutex_trylock_spin(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t, int spincnt = 1)
{
  return Mutex_trylock_spin(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t, spincnt);
}

inline int
Mutex_lock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  ProxyMutex *m, EThread *t)
{
  ink_assert(t != nullptr);
  if (m->thread_holding != t) {
    ink_mutex_acquire(&m->the_mutex);
    m->thread_holding = t;
    ink_assert(m->thread_holding);
#ifdef DEBUG
    m->srcloc    = location;
    m->handler   = ahandler;
    m->hold_time = Thread::get_hrtime();
#ifdef MAX_LOCK_TAKEN
    m->taken++;
#endif // MAX_LOCK_TAKEN
#endif // DEBUG
  }
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  m->blocking_acquires++;
  m->total_acquires++;
  m->print_lock_stats(0);
#endif // LOCK_CONTENTION_PROFILING
#endif // DEBUG
  m->nthread_holding++;
  return true;
}

inline int
Mutex_lock(
#ifdef DEBUG
  const SourceLocation &location, const char *ahandler,
#endif
  Ptr<ProxyMutex> &m, EThread *t)
{
  return Mutex_lock(
#ifdef DEBUG
    location, ahandler,
#endif
    m.get(), t);
}

inline void
Mutex_unlock(ProxyMutex *m, EThread *t)
{
  if (m->nthread_holding) {
    ink_assert(t == m->thread_holding);
    m->nthread_holding--;
    if (!m->nthread_holding) {
#ifdef DEBUG
      if (Thread::get_hrtime() - m->hold_time > MAX_LOCK_TIME)
        lock_holding(m->srcloc, m->handler);
#ifdef MAX_LOCK_TAKEN
      if (m->taken > MAX_LOCK_TAKEN)
        lock_taken(m->srcloc, m->handler);
#endif // MAX_LOCK_TAKEN
      m->srcloc  = SourceLocation(nullptr, nullptr, 0);
      m->handler = nullptr;
#endif // DEBUG
      ink_assert(m->thread_holding);
      m->thread_holding = nullptr;
      ink_mutex_release(&m->the_mutex);
    }
  }
}

inline void
Mutex_unlock(Ptr<ProxyMutex> &m, EThread *t)
{
  Mutex_unlock(m.get(), t);
}


/**
 * ATS 的锁被设计为自动锁，在如下 class MutexLock 和 class MutexTryLock 
 * 两个类的定义中可以看到，析构函数中都调用了 Mutex_unlock。回到上面看
 * 上锁使用的宏，这些宏都是使用 class MutexLock 或 class MutexTryLock
 * 来声明了一个临时的类实例，凡是临时变量总有作用域的限制，所有离开作用域的
 * 时候，就会调用析构函数，因此锁就自动释放了，所以不需要显示释放锁。
 */
/** Scoped lock class for ProxyMutex
 */
class MutexLock
{
private:
  Ptr<ProxyMutex> m;
  bool locked_p;

public:
  MutexLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t)
    : m(am), locked_p(true)
  {
    Mutex_lock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t)
    : m(am), locked_p(true)
  {
    Mutex_lock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  void
  release()
  {
    if (locked_p) {
      Mutex_unlock(m, m->thread_holding);
    }
    locked_p = false;
  }

  ~MutexLock() { this->release(); }
};

/**
 * TryLock 的特殊用法
 * 当使用 MUTEX_TRY_LOCK 之后，发现没有获得锁，但是后面有需要上锁的情况
 * 则该如何处理？
 * 通常情况可能会这样做：
 *    - 通过显示解锁，释放锁的对象
 *    - 然后重新调用 SCOPED_MUTEX_LOCK 来上锁
 * 但是 TryLock 的设计，为这种特殊情况增加了一个方法 acquire()
 *    - 可以直接调用 _l.acquire(ethread) 实现阻塞，直到加锁成功
 *    - 但是要小心使用
 */
/** Scoped try lock class for ProxyMutex
 */
class MutexTryLock
{
private:
  Ptr<ProxyMutex> m;
  bool lock_acquired;

public:
  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t)
    : m(am)
  {
    lock_acquired = Mutex_trylock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t)
    : m(am)
  {
    lock_acquired = Mutex_trylock(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    ProxyMutex *am, EThread *t, int sp)
    : m(am)
  {
    lock_acquired = Mutex_trylock_spin(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t, sp);
  }

  MutexTryLock(
#ifdef DEBUG
    const SourceLocation &location, const char *ahandler,
#endif // DEBUG
    Ptr<ProxyMutex> &am, EThread *t, int sp)
    : m(am)
  {
    lock_acquired = Mutex_trylock_spin(
#ifdef DEBUG
      location, ahandler,
#endif // DEBUG
      m.get(), t, sp);
  }

  ~MutexTryLock()
  {
    if (lock_acquired) {
      Mutex_unlock(m.get(), m->thread_holding);
    }
  }

  /** Spin till lock is acquired
   */
  void
  acquire(EThread *t)
  {
    MUTEX_TAKE_LOCK(m.get(), t);
    lock_acquired = true;
  }

  void
  release()
  {
    ink_assert(lock_acquired); // generate a warning because it shouldn't be done.
    if (lock_acquired) {
      Mutex_unlock(m.get(), m->thread_holding);
    }
    lock_acquired = false;
  }

  bool
  is_locked() const
  {
    return lock_acquired;
  }

  const ProxyMutex *
  get_mutex() const
  {
    return m.get();
  }
};

inline void
ProxyMutex::free()
{
#ifdef DEBUG
#ifdef LOCK_CONTENTION_PROFILING
  print_lock_stats(1);
#endif
#endif
  ink_mutex_destroy(&the_mutex);
  mutexAllocator.free(this);
}

// TODO should take optional mutex "name" identifier, to pass along to the init() fun
/**
  Creates a new ProxyMutex object.

  This is the preferred mechanism for constructing objects of the
  ProxyMutex class. It provides you with faster allocation than
  that of the normal constructor.

  @return A pointer to a ProxyMutex object appropriate for the build
    environment.

*/
// TODO: 支持可选的 mutex 名称，在调用 init() 时传入
/**
 * 创建一个新的 ProxyMutex 对象的首选方法。它可以快速的分配对象占用的内存.
 */
inline ProxyMutex *
new_ProxyMutex()
{
  ProxyMutex *m = mutexAllocator.alloc();
  m->init();
  return m;
}
