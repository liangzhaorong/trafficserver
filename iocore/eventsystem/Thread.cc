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

/****************************************************************************

  Basic Threads



**************************************************************************/
#include "P_EventSystem.h"
#include "tscore/ink_string.h"

///////////////////////////////////////////////
// Common Interface impl                     //
///////////////////////////////////////////////

ink_hrtime Thread::cur_time = ink_get_hrtime_internal();
inkcoreapi ink_thread_key Thread::thread_data_key;

namespace
{
static bool initialized ATS_UNUSED = ([]() -> bool {
  // File scope initialization goes here.
  ink_thread_key_create(&Thread::thread_data_key, nullptr);
  return true;
})();
}

/**
 * - 用于初始化 mutex
 * - 并且上锁
 * - 线程总是保持对其自身 mutex 的锁定
 * - 这样凡是复用线程 mutex 对象，则相当于被线程永久锁定，成为只属于此线程的本地对象
 */
Thread::Thread()
{
  mutex = new_ProxyMutex();
  MUTEX_TAKE_LOCK(mutex, (EThread *)this);
  mutex->nthread_holding += THREAD_MUTEX_THREAD_HOLDING;
}

Thread::~Thread()
{
  ink_release_assert(mutex->thread_holding == (EThread *)this);
  mutex->nthread_holding -= THREAD_MUTEX_THREAD_HOLDING;
  MUTEX_UNTAKE_LOCK(mutex, (EThread *)this);
}

///////////////////////////////////////////////
// Unix & non-NT Interface impl              //
///////////////////////////////////////////////

struct thread_data_internal {
  ThreadFunction f;                  ///< Function to excecute in the thread.
  Thread *me;                        ///< The class instance.
  char name[MAX_THREAD_NAME_LENGTH]; ///< Name for the thread.
};

static void *
spawn_thread_internal(void *a)
{
  auto *p = static_cast<thread_data_internal *>(a);

  // 将 Thread 对象 p->me 通过 key 设置为当前线程的特有数据
  p->me->set_specific();
  ink_set_thread_name(p->name);

  // 若函数指针 f 不为 NULL，则调用 f
  if (p->f) {
    p->f();
  } else {
    // 否则执行线程的默认运行函数
    p->me->execute();
  }

  // 返回后释放对象 p
  delete p;
  return nullptr;
}

void
Thread::start(const char *name, void *stack, size_t stacksize, ThreadFunction const &f)
{
  // 为当前线程的上下文分配内存并初始化
  auto *p = new thread_data_internal{f, this, ""};

  // 线程的名字
  ink_zero(p->name);
  ink_strlcpy(p->name, name, MAX_THREAD_NAME_LENGTH);
  // 若没有指定线程的栈大小，则使用默认的栈大小
  if (stacksize == 0) {
    stacksize = DEFAULT_STACKSIZE;
  }
  // 创建线程时，传入 spawn_thread_internal 函数，这样线程启动会立即调用该函数
  ink_thread_create(&tid, spawn_thread_internal, p, 0, stacksize, stack);
}
