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



****************************************************************************/
#pragma once

#include "I_Thread.h"

///////////////////////////////////////////////
// Common Interface impl                     //
///////////////////////////////////////////////

TS_INLINE void
Thread::set_specific()
{
  // 把当前对象（this）关联到 key
  ink_thread_setspecific(Thread::thread_data_key, this);
}

/**
 * 关于 this_thread()
 *   - 返回当前 Thread 对象
 *   - 由于 EThread 继承自 Thread，因此
 *       - 在 EThread 中调用 this_thread() 返回的是 EThread 对象
 *       - 但是返回值的类型仍然是 Thread *
 *       - 为了提供正确的返回值类型，在 EThread 中又定义了 this_ethread() 方法
 * 
 * 使用同一个 key，在不同的线程中，获得的数据是不同的，同样的，绑定的数据也只与
 * 当前线程关联。因此，对 thread specific 相关函数的调用是与其所在的线程紧密相关的。
 */
TS_INLINE Thread *
this_thread()
{
  // 通过 key 取回之前关联的对象
  return (Thread *)ink_thread_getspecific(Thread::thread_data_key);
}
