/** @file

  Fast-Allocators

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

  Provides three classes
    - Allocator for allocating memory blocks of fixed size
    - ClassAllocator for allocating objects
    - SpaceClassAllocator for allocating sparce objects (most members uninitialized)

  These class provides a efficient way for handling dynamic allocation.
  The fast allocator maintains its own freepool of objects from
  which it doles out object. Allocated objects when freed go back
  to the free pool.

  @note Fast allocators could accumulate a lot of objects in the
  free pool as a result of bursty demand. Memory used by the objects
  in the free pool never gets freed even if the freelist grows very
  large.

 */

#pragma once

#include <new>
#include <cstdlib>
#include "tscore/ink_queue.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_resource.h"
#include <execinfo.h>

#define RND16(_x) (((_x) + 15) & ~15)

extern int cmd_disable_pfreelist;

/**
 * Allocator 是 ATS 内部使用的一种内存管理技术，主要用来减少内存分配次数，
 * 从而提高性能。另外，通过把相同尺寸的内存块连续存放，也减少了内存的碎片
 * 化。
 *
 * Allocator 类是所有 ClassAllocator 类的基类：
 *   - free pool
 *       - 内存块有一个名字，在构造函数中设置
 *       - 至少包含一个内存单元
 *       - 内存单元的尺寸由：element_size * chunk_size 决定
 *       - 该内存单元由 chunk_size 个 element_size 字节的 "小块内存" 组成
 *       - chunk_size 默认为 128 个
 *   - 它有三个方法
 *       - alloc_void 调用 ink_freelist_new 从 free pool 获得一个固定尺寸的内存块
 *       - free_void 调用 ink_freelist_free 将一个内存块归还给 free pool
 *       - free_void_bulk 调用 ink_freelist_bulk 将一组内存块归还给 free pool
 *   - 构造函数调用 ink_freelist_init 创建一整块内存，支持地址空间对齐
 *
 */

/** Allocator for fixed size memory blocks. */
class Allocator
{
public:
  /**
    Allocate a block of memory (size specified during construction
    of Allocator.
  */
  void *
  alloc_void()
  {
    return ink_freelist_new(this->fl, freelist_class_ops);
  }

  /**
    Deallocate a block of memory allocated by the Allocator.

    @param ptr pointer to be freed.
  */
  void
  free_void(void *ptr)
  {
    ink_freelist_free(this->fl, ptr, freelist_class_ops);
  }

  /**
    Deallocate blocks of memory allocated by the Allocator.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks to be freed.
  */
  void
  free_void_bulk(void *head, void *tail, size_t num_item)
  {
    ink_freelist_free_bulk(this->fl, head, tail, num_item, freelist_class_ops);
  }

  Allocator() { fl = nullptr; }

  /**
    Creates a new allocator.

    @param name identification tag used for mem tracking .
    @param element_size size of memory blocks to be allocated.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.
  */
  Allocator(const char *name, unsigned int element_size, unsigned int chunk_size = 128, unsigned int alignment = 8)
  {
    ink_freelist_init(&fl, name, element_size, chunk_size, alignment);
  }

  /** Re-initialize the parameters of the allocator. */
  void
  re_init(const char *name, unsigned int element_size, unsigned int chunk_size, unsigned int alignment, int advice)
  {
    ink_freelist_madvise_init(&this->fl, name, element_size, chunk_size, alignment, advice);
  }

protected:
  InkFreeList *fl;
};


/**
 * ClassAllocator 是一个 C++ 的类模板，继承自 Allocator 基类：
 *   - 扩展了三个方法：alloc(), free(), free_bulk()
 *       - 输入指针为 Class* 类型，主要是为了适配构造函数
 *   - 与 Allocator 的区别就是 element_size = sizeof(Class)
 *
 * ATS 中时候到的各种数据结构，特别是需要在 EventSystem 与各种状态机之间
 * 进行传递的数据结构，都需要分配内存。对于 ATS 这种服务器系统，每一个会话
 * 进来的时候动态的分配内存，会话结束后释放内存，这种对内存的操作频率是非常
 * 高的。
 *
 * 而且在会话存续期间，对于各种对象会有各种不同尺寸的内存空间被分配和释放，
 * 内存的碎片化也非常严重。
 *
 * 所有的数据都使用 Allocator 类来分配，不符合面向对象的设计习惯，因此 ATS 
 * 设计了 ClassAllocator 模板。
 *
 * 使用 ClassAllocator 模板为指定的数据结构类型（class C）定义了一个全局实例，
 * 该实例内部有一个 proto 结构体，该结构体中 typeObject 是一个 class C 类型
 * 的成员.
 *
 * ClassAllocator 的构造函数负责从操作系统分配到的大块内存，如每个内存块 16 MB，
 * 然后按照 proto.typeObject 对象的尺寸切成小块。每一个大块内存将按照 sizeof(C)
 * 为最小单位，分割为 chunk_size 个小单元，这些小单元被放入一个 free pool
 * (InkFreeList *fl) 暂存。
 *
 * 然后，通过 ClassAllocator::alloc() 从 free pool 获取一个小块内存单元，
 * ClassAllocator::free() 则用于归还小块内存单元。
 *
 * 这样就减少了直接通过操作系统进行内存分配的次数，而且把相同类型的对象连续存放，
 * 减少了内存的碎片化。
 *
 * 1. 使用
 * 1.1 定义一个全局实例
 * 
 *   ClassAllocator<NameOfClass> nameofclassAllocator("nameofclassAllocator", 256)
 *
 * 第一个参数是内存块的名字，第二个参数 256 表示 Allocator 创建大块内存时，每个大块
 * 内存可以存放 256 个 NameOfClass 对象。
 *
 * 1.2 为 NameOfClass 类型的对象分配一个实例
 *
 *   NameOfClass *obj = nameofclassAllocator.alloc();
 *   obj->init(...);
 *
 * alloc() 返回的对象，其构造函数已经执行过，再次调用 init() 是为了初始化指针类型
 * 的成员。
 *
 * 1.3 回收内存
 * 
 *   obj->clear();
 *   nameofclassAllocator.free(obj);
 *   obj = NULL;
 *
 * 使用 clear() 方法，主要是为了释放指针类型的成员指向的内存对象。
 * 或者调用 destroy() 方法：
 * 
 *   obj->destroy();
 *   obj = NULL;
 *
 */

/**
  Allocator for Class objects. It uses a prototype object to do
  fast initialization. Prototype of the template class is created
  when the fast allocator is created. This is instantiated with
  default (no argument) constructor. Constructor is not called for
  the allocated objects. Instead, the prototype is just memory
  copied onto the new objects. This is done for performance reasons.

*/
template <class C> class ClassAllocator : public Allocator
{
public:
  /** Allocates objects of the templated type. */
  C *
  alloc()
  {
    void *ptr = ink_freelist_new(this->fl, freelist_class_ops);

    // 直接将 proto.typeObject 对象复制到新的内存区域完成对象的创建
    memcpy(ptr, (void *)&this->proto.typeObject, sizeof(C));
    return (C *)ptr;
  }

  /**
    Deallocates objects of the templated type.

    @param ptr pointer to be freed.
  */
  void
  free(C *ptr)
  {
    // 直接将内存块归还至大块内存.
    ink_freelist_free(this->fl, ptr, freelist_class_ops);
  }

  /**
    Deallocates objects of the templated type.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks to be freed.
   */
  void
  free_bulk(C *head, C *tail, size_t num_item)
  {
    ink_freelist_free_bulk(this->fl, head, tail, num_item, freelist_class_ops);
  }

  /**
    Allocate objects of the templated type via the inherited interface
    using void pointers.
  */
  void *
  alloc_void()
  {
    return (void *)alloc();
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param ptr pointer to be freed.
  */
  void
  free_void(void *ptr)
  {
    free((C *)ptr);
  }

  /**
    Deallocate objects of the templated type via the inherited
    interface using void pointers.

    @param head pointer to be freed.
    @param tail pointer to be freed.
    @param num_item of blocks.
  */
  void
  free_void_bulk(void *head, void *tail, size_t num_item)
  {
    free_bulk((C *)head, (C *)tail, num_item);
  }

  /**
    Create a new class specific ClassAllocator.

    @param name some identifying name, used for mem tracking purposes.
    @param chunk_size number of units to be allocated if free pool is empty.
    @param alignment of objects must be a power of 2.
  */
  ClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16)
  {
    ::new ((void *)&proto.typeObject) C();
    ink_freelist_init(&this->fl, name, RND16(sizeof(C)), chunk_size, RND16(alignment));
  }

  struct {
    uint8_t typeObject[sizeof(C)];
    int64_t space_holder = 0;
  } proto;
};

template <class C> class TrackerClassAllocator : public ClassAllocator<C>
{
public:
  TrackerClassAllocator(const char *name, unsigned int chunk_size = 128, unsigned int alignment = 16)
    : ClassAllocator<C>(name, chunk_size, alignment), allocations(0), trackerLock(PTHREAD_MUTEX_INITIALIZER)
  {
  }

  C *
  alloc()
  {
    void *callstack[3];
    int frames = backtrace(callstack, 3);
    C *ptr     = ClassAllocator<C>::alloc();

    const void *symbol = nullptr;
    if (frames == 3 && callstack[2] != nullptr) {
      symbol = callstack[2];
    }

    tracker.increment(symbol, (int64_t)sizeof(C), this->fl->name);
    ink_mutex_acquire(&trackerLock);
    reverse_lookup[ptr] = symbol;
    ++allocations;
    ink_mutex_release(&trackerLock);

    return ptr;
  }

  void
  free(C *ptr)
  {
    ink_mutex_acquire(&trackerLock);
    std::map<void *, const void *>::iterator it = reverse_lookup.find(ptr);
    if (it != reverse_lookup.end()) {
      tracker.increment((const void *)it->second, (int64_t)sizeof(C) * -1, nullptr);
      reverse_lookup.erase(it);
    }
    ink_mutex_release(&trackerLock);
    ClassAllocator<C>::free(ptr);
  }

private:
  ResourceTracker tracker;
  std::map<void *, const void *> reverse_lookup;
  uint64_t allocations;
  ink_mutex trackerLock;
};
