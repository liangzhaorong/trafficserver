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

  ProxyAllocator.h



*****************************************************************************/
#pragma once

#include "tscore/ink_platform.h"

class EThread;

extern int thread_freelist_high_watermark;
extern int thread_freelist_low_watermark;
extern int cmd_disable_pfreelist;

/**
 * ProxyAllocator 是线程访问 ClassAllocator 的缓存，这个类是访问 ClassAllocator 
 * 的一个 Proxy 实现。这个类本身并不负责分配和释放内存，而只是维护了上层 
 * ClassAllocator 的一个可用内存块的列表。
 *
 * 1. 构成
 * ProxyAllocator
 *   - 该结构维护一个可用 "小块内存" 的链表 freelist
 *   - 和一个表示该链表内有多少元素的计数器 allocated
 * 2. 设计思想
 * ATS 是一个多线程的系统，Allocator 和 ClassAllocator 是全局数据，那么多线程访问
 * 全局数据，必然要加锁和解锁，这样效率就会比较低。
 * 于是 ATS 设计了一个 ProxyAllocator，它是每一个 Thread 内部的数据结构，它将 Thread
 * 对 ClassAllocator 全局数据的访问进行了一个 Proxy 操作。
 *   - Thread 请求内存时调用 THREAD_ALLOC 方法（其实是一个宏定义），判断 ProxyAllocator 
 *     里是否有可用的内存块
 *       - 如果没有
 *           - 调用对应的 ClassAllocator 的 alloc 方法申请一块内存
 *           - 此操作将从大块的内存里取出一个未分配的小块内存
 *       - 如果有
 *           - 直接从 ProxyAllocator 里拿一个，freelist 指向下一个可用内存块，allocated--
 *   - Thread 释放内存时调用 THREAD_FREE 方法（其实是一个宏定义）
 *       - 直接把该内存地址放入 ProxyAllocator 的 freelist 里，allocated++
 *       - 注意：此时，这块内存并未被在 ClassAllocator 里标记为可分配状态
 * 从上述过程可知，ProxyAllocator 就是不断的将全局空间据为已有。
 * 那么就会出现一种比较恶劣的情况：
 *   - 某个 Thread 由于在执行特殊操作时，将大量的全局空间据为已有
 *   - 导致了 Thread 间内存分配不均衡
 * 对于这种情况，ATS 也做了处理：
 *   - 参数：thread_freelist_low_watermark 和 thread_freelist_high_watermark
 *   - 在 THREAD_FREE 内部，会做一个判断：如果 ProxyAllocator 和 freelist 持有的内存片
 *     超过了 High Watermark 值
 *       - 就调用 thread_freeup 方法
 *           - 从 freelist 的头部删除连续的数个节点，直到 freelist 只剩下 Low Watermark 个节点
 *           - 调用 ClassAllocator 的 free 或 free_bulk，将从 freelist 里删除的节点标记为可分配内存块
 *
 * 总结：
 *   - 凡是直接操作 ProxyAllocator 的内部元素 freelist
 *       - 都是不需要加锁和解锁的，因为那是 Thread 内部数据
 *   - 但是需要 ClassAllocator 介入的
 *       - 都是需要加锁和解锁的
 *       - 当 ProxyAllocator 的 freelist 指向 NULL 时
 *       - 当 allocated 大于 thread_freelist_high_watermark 时
 *   - 通过 ProxyAllocator
 *       - Thread 在访问全局内存池的资源时，可以有较少的资源锁冲突
 */

struct ProxyAllocator {
  int allocated;
  void *freelist;

  ProxyAllocator() : allocated(0), freelist(nullptr) {}
};

template <class C>
inline C *
thread_alloc(ClassAllocator<C> &a, ProxyAllocator &l)
{
  if (!cmd_disable_pfreelist && l.freelist) {
    C *v       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    *(void **)v = *(void **)&a.proto.typeObject;
    return v;
  }
  return a.alloc();
}

template <class C>
inline C *
thread_alloc_init(ClassAllocator<C> &a, ProxyAllocator &l)
{
  if (!cmd_disable_pfreelist && l.freelist) {
    C *v       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    memcpy((void *)v, (void *)&a.proto.typeObject, sizeof(C));
    return v;
  }
  return a.alloc();
}

template <class C>
inline void
thread_free(ClassAllocator<C> &a, C *p)
{
  a.free(p);
}

static inline void
thread_free(Allocator &a, void *p)
{
  a.free_void(p);
}

template <class C>
inline void
thread_freeup(ClassAllocator<C> &a, ProxyAllocator &l)
{
  C *head      = (C *)l.freelist;
  C *tail      = (C *)l.freelist;
  size_t count = 0;
  while (l.freelist && l.allocated > thread_freelist_low_watermark) {
    tail       = (C *)l.freelist;
    l.freelist = *(C **)l.freelist;
    --(l.allocated);
    ++count;
  }

  if (unlikely(count == 1)) {
    a.free(tail);
  } else if (count > 0) {
    a.free_bulk(head, tail, count);
  }

  ink_assert(l.allocated >= thread_freelist_low_watermark);
}

void *thread_alloc(Allocator &a, ProxyAllocator &l);
void thread_freeup(Allocator &a, ProxyAllocator &l);

#define THREAD_ALLOC(_a, _t) thread_alloc(::_a, _t->_a)
#define THREAD_ALLOC_INIT(_a, _t) thread_alloc_init(::_a, _t->_a)
#define THREAD_FREE(_p, _a, _t)                              \
  if (!cmd_disable_pfreelist) {                              \
    do {                                                     \
      *(char **)_p    = (char *)_t->_a.freelist;             \
      _t->_a.freelist = _p;                                  \
      _t->_a.allocated++;                                    \
      if (_t->_a.allocated > thread_freelist_high_watermark) \
        thread_freeup(::_a, _t->_a);                         \
    } while (0);                                             \
  } else {                                                   \
    thread_free(::_a, _p);                                   \
  }

/**
 * freelist 这个链表里只保存可用内存块
 *   - 在这个链表里的内存空间里的数据都是没用的
 * 为了节省内存的使用，freelist 将链表内下一个内存块的指针地址
 * 存储在了 freelist[0] 的位置。
 * 所以会看到如下代码：
 *   l.freelist = *(void **)l.freelist;
 * 大约等价于：
 *   l.freelist = (void *)l.freelist[0];
 * 
 *   *(char **)_p = (char *)_t->_a.freelist;
 * 大约等价于：
 *   (char *)_p[0] = (char *)_t->_a.freelist;
 */
