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

#include "tscore/ink_align.h"
#include "I_EventProcessor.h"

const int LOAD_BALANCE_INTERVAL = 1;

/**
 * 在每一个 EThread 内部都有一个 thread_private 数组，用来存储线程的私有数据.
 * 
 * 但是要保证每一次分配，所以的 EThread 线程都要同步，如果进入到每一个 EThread 内部进行分配，
 * 就要调用多次分配过程，EventProcessor 中提供了一个方法用来进行分配这个空间。
 *
 * 所有 EThread 的 thread_private 区域，都是一样大小的，那么只需要为每一次分配指定一个偏移量（offset），
 * 每个 EThread 内部操作的时候，通过这个偏移量访问 thread_private 区域，就可以保证一次分配，所有
 * EThread 都进行了分配。为了保证内存访问效率，该方法在分配时还进行内存对齐。
 */
TS_INLINE off_t
EventProcessor::allocate(int size)
{
  // offsetof(EThread, thread_private) 方法返回 thread_private 成员在 class EThread 中的相对偏移量
  // 通过 INK_ALIGN 进行向上对齐
  static off_t start = INK_ALIGN(offsetof(EThread, thread_private), 16);
  // 因为内存对齐就会浪费掉 thread_private 开头的一小部分空间，计算出这个空间的大小
  static off_t loss  = start - offsetof(EThread, thread_private);
  // 然后再使用 INK_ALIGN 对即将分配的空间尺寸 size 进行向上对齐
  size               = INK_ALIGN(size, 16); // 16 byte alignment

  // 接下来是进行原子操作，分配空间
  // 使用 thread_data_used 来记录已经被分配的内存空间
  int old;
  do {
    old = thread_data_used;
    if (old + loss + size > PER_THREAD_DATA) {
      // 没有空间可以分配时，返回 -1
      return -1;
    }
  } while (!ink_atomic_cas(&thread_data_used, old, old + size));

  // 分配成功，返回偏移量，此时：
  //   thread_data_used 已经指向未分配区域的第一个地址
  //   start 是静态变量，永远指向对齐后的 thread_private 第一个地址
  //   old 则保存了上一次的 thread_data_used 的值
  return (off_t)(old + start);
}

// 当通过 EventProcessor.schedule_*() 方法将事件（Event）放入线程组时，如何决定由哪个
// 线程（EThread）来处理呢？
// 实现的机制是：每次创建事件（Event）时会调用 assign_thread() 来获取一个指向 EThread
// 的指针，然后就由这个线程（EThread）来处理该事件（Event）
TS_INLINE EThread *
EventProcessor::assign_thread(EventType etype)
{
  int next;
  ThreadGroupDescriptor *tg = &thread_group[etype];

  ink_assert(etype < MAX_EVENT_TYPES);
  if (tg->_count > 1) {
    // When "_next_round_robin" grows big enough, it becomes a negative number,
    // meaning "next" is also negative. And since "next" is used as an index
    // into array "_thread", the result is returning NULL when assigning threads.
    // So we need to cast "_next_round_robin" to unsigned int so the result stays
    // positive.
    next = static_cast<unsigned int>(++tg->_next_round_robin) % tg->_count;
  } else {
    next = 0;
  }
  // 通过 _thread 数组，得到下一个 EThread 的指针
  return tg->_thread[next];
}

// 在 schedule_*() 中会通过 EventQueueExternal.enqueue(e, fast_signal = true) 把 event 放入外部队列
TS_INLINE Event *
EventProcessor::schedule(Event *e, EventType etype, bool fast_signal)
{
  ink_assert(etype < MAX_EVENT_TYPES);
  // assign_thread(etype) 负责从线程列表中返回指定 etype 类型的 ethread 线程指针
  e->ethread = assign_thread(etype);
  // 如果 Cont 有自己的 mutex 则 Event 继承 Cont 的 mutex，
  // 否则 Event 和 Cont 都要继承 ethread 的 mutex
  if (e->continuation->mutex) {
    e->mutex = e->continuation->mutex;
  } else {
    e->mutex = e->continuation->mutex = e->ethread->mutex;
  }
  // 将 Event 压入所分配的 ethread 线程的外部队列
  e->ethread->EventQueueExternal.enqueue(e, fast_signal);
  return e;
}

TS_INLINE Event *
EventProcessor::schedule_imm_signal(Continuation *cont, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();

  ink_assert(et < MAX_EVENT_TYPES);
#ifdef ENABLE_TIME_TRACE
  e->start_time = Thread::get_hrtime();
#endif
  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, 0, 0), et, true);
}

TS_INLINE Event *
EventProcessor::schedule_imm(Continuation *cont, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();

  ink_assert(et < MAX_EVENT_TYPES);
#ifdef ENABLE_TIME_TRACE
  e->start_time = Thread::get_hrtime();
#endif
  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, 0, 0), et);
}

TS_INLINE Event *
EventProcessor::schedule_at(Continuation *cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();

  ink_assert(t > 0);
  ink_assert(et < MAX_EVENT_TYPES);
  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, t, 0), et);
}

TS_INLINE Event *
EventProcessor::schedule_in(Continuation *cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();

  ink_assert(et < MAX_EVENT_TYPES);
  e->callback_event = callback_event;
  e->cookie         = cookie;
  return schedule(e->init(cont, Thread::get_hrtime() + t, 0), et);
}

TS_INLINE Event *
EventProcessor::schedule_every(Continuation *cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();

  ink_assert(t != 0);
  ink_assert(et < MAX_EVENT_TYPES);
  e->callback_event = callback_event;
  e->cookie         = cookie;
  if (t < 0) {
    return schedule(e->init(cont, t, t), et);
  } else {
    return schedule(e->init(cont, Thread::get_hrtime() + t, t), et);
  }
}
