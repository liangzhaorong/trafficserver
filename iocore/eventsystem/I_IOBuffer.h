/** @file

  I/O classes

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

  @section watermark Watermark

  Watermarks can be used as an interface between the data transferring
  layer (VConnection) and the user layer (a state machine).  Watermarks
  should be used when you need to have at least a certain amount of data
  to make some determination.  For example, when parsing a string, one
  might wish to ensure that an entire line will come in before consuming
  the data.  In such a case, the water_mark should be set to the largest
  possible size of the string. (appropriate error handling should take
  care of exessively long strings).

  In all other cases, especially when all data will be consumed, the
  water_mark should be set to 0 (the default).

 */

#pragma once
#define I_IOBuffer_h

#include "tscore/ink_platform.h"
#include "tscore/ink_apidefs.h"
#include "tscore/Allocator.h"
#include "tscore/Ptr.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_resource.h"

struct MIOBufferAccessor;

class MIOBuffer;
class IOBufferReader;
class VIO;

// Removing this optimization since this is breaking WMT over HTTP
//#define WRITE_AND_TRANSFER

inkcoreapi extern int64_t max_iobuffer_size;
extern int64_t default_small_iobuffer_size;
extern int64_t default_large_iobuffer_size; // matched to size of OS buffers

#if !defined(TRACK_BUFFER_USER)
#define TRACK_BUFFER_USER 1
#endif

enum AllocType {
  NO_ALLOC,
  FAST_ALLOCATED,
  XMALLOCED,
  MEMALIGNED,
  DEFAULT_ALLOC,
  CONSTANT,
};

#define DEFAULT_BUFFER_NUMBER 128
#define DEFAULT_HUGE_BUFFER_NUMBER 32
#define MAX_MIOBUFFER_READERS 5
#define DEFAULT_BUFFER_ALIGNMENT 8192 // should be disk/page size
#define DEFAULT_BUFFER_BASE_SIZE 128

////////////////////////////////////////////////
// These are defines so that code that used 2 //
// for buffer size index when 2 was 2K will   //
// still work if it uses BUFFER_SIZE_INDEX_2K //
// instead.                                   //
////////////////////////////////////////////////
/* 在宏定义 MAX_BUFFER_SIZE_INDEX 中，ATS 定义了如下几种尺寸的 buf */
#define BUFFER_SIZE_INDEX_128 0
#define BUFFER_SIZE_INDEX_256 1
#define BUFFER_SIZE_INDEX_512 2
#define BUFFER_SIZE_INDEX_1K 3
#define BUFFER_SIZE_INDEX_2K 4
#define BUFFER_SIZE_INDEX_4K 5
#define BUFFER_SIZE_INDEX_8K 6
#define BUFFER_SIZE_INDEX_16K 7
#define BUFFER_SIZE_INDEX_32K 8
#define BUFFER_SIZE_INDEX_64K 9
#define BUFFER_SIZE_INDEX_128K 10
#define BUFFER_SIZE_INDEX_256K 11
#define BUFFER_SIZE_INDEX_512K 12
#define BUFFER_SIZE_INDEX_1M 13
#define BUFFER_SIZE_INDEX_2M 14
#define MAX_BUFFER_SIZE_INDEX 14
#define DEFAULT_BUFFER_SIZES (MAX_BUFFER_SIZE_INDEX + 1)

#define BUFFER_SIZE_FOR_INDEX(_i) (DEFAULT_BUFFER_BASE_SIZE * (1 << (_i)))
#define DEFAULT_SMALL_BUFFER_SIZE BUFFER_SIZE_INDEX_512
#define DEFAULT_LARGE_BUFFER_SIZE BUFFER_SIZE_INDEX_4K
#define DEFAULT_TS_BUFFER_SIZE BUFFER_SIZE_INDEX_8K
#define DEFAULT_MAX_BUFFER_SIZE BUFFER_SIZE_FOR_INDEX(MAX_BUFFER_SIZE_INDEX)
#define MIN_IOBUFFER_SIZE BUFFER_SIZE_INDEX_128
#define MAX_IOBUFFER_SIZE (DEFAULT_BUFFER_SIZES - 1)

#define BUFFER_SIZE_ALLOCATED(_i) (BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_i) || BUFFER_SIZE_INDEX_IS_XMALLOCED(_i))

#define BUFFER_SIZE_NOT_ALLOCATED DEFAULT_BUFFER_SIZES
#define BUFFER_SIZE_INDEX_IS_XMALLOCED(_size_index) (_size_index < 0)
#define BUFFER_SIZE_INDEX_IS_FAST_ALLOCATED(_size_index) (((uint64_t)_size_index) < DEFAULT_BUFFER_SIZES)
#define BUFFER_SIZE_INDEX_IS_CONSTANT(_size_index) (_size_index >= DEFAULT_BUFFER_SIZES)

#define BUFFER_SIZE_FOR_XMALLOC(_size) (-(_size))
#define BUFFER_SIZE_INDEX_FOR_XMALLOC_SIZE(_size) (-(_size))

#define BUFFER_SIZE_FOR_CONSTANT(_size) (_size - DEFAULT_BUFFER_SIZES)
#define BUFFER_SIZE_INDEX_FOR_CONSTANT_SIZE(_size) (_size + DEFAULT_BUFFER_SIZES)

inkcoreapi extern Allocator ioBufAllocator[DEFAULT_BUFFER_SIZES];

void init_buffer_allocators(int iobuffer_advice);

/*
 * IOBuffer 系统的组成
 * 
 * 内存管理层由 IOBufferData 和 IOBufferBlock 实现
 * - 每一个 IOBufferBlock 内都有一个指向 IOBufferData 的指针
 * - 每一个 IOBufferBlock 都有一个可以指向下一个 IOBufferBlock 的指针
 * - 下面当提及 IOBufferBlock 的时候，通常是指整个 IOBufferBlock 的链表，而不是一个 Block
 * 
 * 数据操作层由 IOBufferReader 和 MIOBuffer 实现，这样不用去直接处理 IOBufferBlock 链表和 IOBufferData 结构。
 * - IOBufferReader 负责读取 IOBufferBlock 里的数据（消费者）
 * - MIOBuffer 则可以向 IOBufferBlock 中写入数据（生产者）
 * - 同时 MIOBuffer 也可以包含多个 IOBufferReader 的指针
 *     - 这样可以记录一个内存数据，多个消费者的信息和消费情况
 *
 * 数据访问抽象层由 MIOBufferAccessor 实现
 * - MIOBufferAccessor 包含了对一个特定的 IOBufferBlock 的读、写操作
 *     - 一个指向 IOBufferReader 的指针
 *     - 一个指向 MIOBuffer 的指针
 *
      IOBufferReader  -----[entry.read]------+ 
            ||     ||                            V
          block   mbuf                   MIOBufferAccessor
            ||     ||                            |
            ||  MIOBuffer <----[mbuf.write]------+
            ||     ||
            ||  _writer
            ||     ||
          IOBufferBlock ===> IOBufferBlock ===> IOBufferBlock ===> NULL
               ||                 ||                 ||
             _data              _data              _data
               ||                 ||                 ||
          IOBufferData       IOBufferData       IOBufferData

    || or = means member reference
    |  or - means method path
 *
 * 总结：
 * - 当需要一个缓冲区存放数据的时候，就创建一个 MIOBuffer 实例
 * - 然后就可以让 MIOBuffer 里面写数据了
 *     - MIOBuffer 会自动创建 IOBufferBlock 和 IOBufferData
 * - 如果需要从缓存区读取数据
 *     - 可以创建 IOBufferReader
 *     - 或者直接通过 MIOBuffer 读取
 * - 当调用 do_io_read 的时候
 *     - 需要传递 MIOBuffer 过去，因为需要把读取到的数据写入 IOBuffer
 * - 当调用 do_io_write 的时候
 *     - 只需要传递 IOBufferReader 过去，因为要读取 IOBuffer 里面的数据发送出去
 * - 当需要将 IOBuffer 传递给另一个方法/函数时，而又不确定对方的操作是读还是写
 *     - 可以传递 MIOBufferAccessor 实例
 *     - 例如：VIO 就包含 MIOBufferAccessor 的实例
 */

/**
  A reference counted wrapper around fast allocated or malloced memory.
  The IOBufferData class provides two basic services around a portion
  of allocated memory.

  First, it is a reference counted object and ...

  @remarks The AllocType enum, is used to define the type of allocation
  for the memory this IOBufferData object manages.

  <table>
    <tr>
      <td align="center">AllocType</td>
      <td align="center">Meaning</td>
    </tr>
    <tr>
      <td>NO_ALLOC</td>
      <td></td>
    </tr>
    <tr>
      <td>FAST_ALLOCATED</td>
      <td></td>
    </tr>
    <tr>
      <td>XMALLOCED</td>
      <td></td>
    </tr>
    <tr>
      <td>MEMALIGNED</td>
      <td></td>
    </tr>
    <tr>
      <td>DEFAULT_ALLOC</td>
      <td></td>
    </tr>
    <tr>
      <td>CONSTANT</td>
      <td></td>
    </tr>
  </table>

 */
/*
 * IOBufferData 是最底层的内存单元，实现了内存的分配和释放，对引用进行计数
 * - 通过继承 RefCountObj，从而支持引用计数器
 * - 为实现引用计数功能，被其它类包含时，总是定义为智能指针，如: Ptr data;
 * - 成员变量 _data 指向通过全局变量 ioBufferAllocator[] 分配的内存空间
 * - AllocType 枚举值定义了该对象管理的对象所采用的内存分配类型
 *     - NO_ALLOC
 *     - FAST_ALLOCATED
 *     - XMALLOCED
 *     - MEMALIGNED
 *     - DEFAULT_ALLOC
 *     - CONSTANT
 * - 可以通过 new_IOBufferData() 创建一个实例
 * - 可以通过 new_xmalloc_IOBufferData() 创建一个实例
 * - 可以通过 new_constant_IOBufferData() 创建一个实例
 * - 它的实例占用的内存空间由 ioBufferData 分配
 */
class IOBufferData : public RefCountObj
{
public:
  /**
    The size of the memory allocated by this IOBufferData. Calculates
    the amount of memory allocated by this IOBufferData.

    @return number of bytes allocated for the '_data' member.

    返回分配的内存大小
  */
  int64_t block_size();

  /**
    Frees the memory managed by this IOBufferData.  Deallocates the
    memory previously allocated by this IOBufferData object. It frees
    the memory pointed to by '_data' according to the '_mem_type' and
    '_size_index' members.
    释放由该 IOBufferData 管理的内存。释放先前由该 IOBufferData 对象分配的内存。
    它根据 `_mem_type` 和 `_size_index` 成员释放 `_data` 指向的内存.

    释放之前由 alloc 分配，由该类管理的内存
  */
  void dealloc();

  /**
    Allocates memory and sets this IOBufferData to point to it.
    Allocates memory according to the size_index and type
    parameters. Any previously allocated memory pointed to by
    this IOBufferData is deallocated.

    @param size_index
    @param type of allocation to use; see remarks section.

    根据 size_index 和 type 来分配内存，如果之前已经分配过，则会先执行 dealloc
  */
  void alloc(int64_t size_index, AllocType type = DEFAULT_ALLOC);

  /**
    Provides access to the allocated memory. Returns the address of the
    allocated memory handled by this IOBufferData.
    提供对已分配内存的访问。返回由该 IOBufferData 处理的已分配内存的地址。

    @return address of the memory handled by this IOBufferData.

  */
  char *
  data()
  {
    return _data;
  }

  /**
    Cast operator. Provided as a convenience, the cast to a char* applied
    to the IOBufferData returns the address of the memory handled by the
    IOBuffer data. In this manner, objects of this class can be used as
    parameter to functions requiring a char*.
    
    重载 char * 操作，返回 _data 成员
  */
  operator char *() { return _data; }
  /**
    Frees the IOBufferData object and its underlying memory. Deallocates
    the memory managed by this IOBufferData and then frees itself. You
    should not use this object or reference after this call.

    释放 IOBufferData 实例自身
    首先执行 dealloc，然后释放 IOBufferData 对象自身（通过 ioDataAllocator 回收内存资源）
    因此，执行该方法后就不能再使用和引用该对象了
  */
  void free() override;

  /* 表示内存块的字节数，通过公式 128*2^size_index 来得出 */
  int64_t _size_index;

  /**
    Type of allocation used for the managed memory. Stores the type of
    allocation used for the memory currently managed by the IOBufferData
    object. Do not set or modify this value directly. Instead use the
    alloc or dealloc methods.

    内存分配类型，AllocType 枚举值
    NO_ALLOC 表示当前未分配，用于延时分配。
  */
  AllocType _mem_type;

  /**
    Points to the allocated memory. This member stores the address of
    the allocated memory. You should not modify its value directly,
    instead use the alloc or dealloc methods.
    指向已分配的内存。不要直接修改该值，而是使用 alloc 或 dealloc 方法修改

  */
  char *_data;

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  /**
    Constructor. Initializes state for a IOBufferData object. Do not use
    this method. Use one of the functions with the 'new_' prefix instead.
    构造函数。初始化 IOBufferData 的状态。不要使用该方法，而是使用带有 'new_' 前缀
    的方法。

  */
  IOBufferData()
    : _size_index(BUFFER_SIZE_NOT_ALLOCATED),
      _mem_type(NO_ALLOC),
      _data(nullptr)
#ifdef TRACK_BUFFER_USER
      ,
      _location(nullptr)
#endif
  {
  }

  // noncopyable, declaration only
  IOBufferData(const IOBufferData &) = delete;
  IOBufferData &operator=(const IOBufferData &) = delete;
};

/* 声明一个全局分配此类型实例的 ClassAllocator */
inkcoreapi extern ClassAllocator<IOBufferData> ioDataAllocator;

/**
  A linkable portion of IOBufferData. IOBufferBlock is a chainable
  buffer block descriptor. The IOBufferBlock represents both the used
  and available space in the underlying block. The IOBufferBlock is not
  sharable between buffers but rather represents what part of the data
  block is both in use and usable by the MIOBuffer it is attached to.
  IOBufferData 的可链接部分。IOBufferBlock 是一个可链接的缓冲区块描述符。
  IOBufferBlock 代表底层块中已用空间和可用空间。IOBufferBlock 在缓冲区之间
  不可共享，而是表示正在使用的和可以被附加到 MIOBufer 使用的数据块部分。

*/
/*
 * IOBufferBlock 用于链接多个 IOBufferData，构成更大的存储单元，实现可伸缩的内存管理.
 * - 是一个单链表，成员智能指针 next 指向下一个 IOBufferBlock
 * - 成员智能指针 data 是一个 IOBufferData 类型的内存块
 * - 通过成员 _start 描述了 IOBufferData 内存块中的数据起始位置
 * - 通过成员 _end 描述了 IOBufferData 内存块中数据结束位置
 * - 通过成员 _buf_end 描述了 IOBufferData 内存块的结束边界，空闲可用空间为 _end 到 _end_buf 之间的部分
 * - 因此一个 IOBufferBlock 是对 IOBufferData 使用情况的描述，同时它提供了数个操作 IOBufferData 的方法
 * - 将多个 IOBufferBlock 连接起来，可以将多个 IOBufferData 里的数据组合成更大的 buf
 * - 通过全局变量 ioBlockAllocator 可以创建 IOBufferBlock 实例
 * - MIOBuffer 通过挂载一个 IOBufferBlock 结构，可以知道缓冲区内的使用情况（哪一部分正在使用以及哪一部分是可用的）
 * - 不能在 buffer 之间共享
 * - 可以通过 new_IOBufferBlock() 创建一个实例
 * - 它的实例占用的内存空间由 IoBlockAllocator 分配
 */
class IOBufferBlock : public RefCountObj
{
public:
  /**
    Access the actual data. Provides access to rhe underlying data
    managed by the IOBufferData.

    @return pointer to the underlying data.

    返回指向底层数据块 IOBufferData 的指针
  */
  char *
  buf()
  {
    return data->_data;
  }

  /**
    Beginning of the inuse section. Returns the position in the buffer
    where the inuse area begins.

    @return pointer to the start of the inuse section.
    返回 _start 成员变量，指向正在使用数据区域的开始位置的指针
  */
  char *
  start()
  {
    return _start;
  }

  /**
    End of the used space. Returns a pointer to end of the used space
    in the data buffer represented by this block.

    @return pointer to the end of the inuse portion of the block.
    返回 _end 成员变量，指向正在使用数据区域的结束位置的指针
  */
  char *
  end()
  {
    return _end;
  }

  /**
    End of the data buffer. Returns a pointer to end of the data buffer
    represented by this block.
    返回 _buf_end 成员变量，指向底层数据块结束位置的指针
  */
  char *
  buf_end()
  {
    return _buf_end;
  }

  /**
    Size of the inuse area. Returns the size of the current inuse area.
    返回正在使用的数据区域的大小

    @return bytes occupied by the inuse area.

  */
  int64_t
  size()
  {
    return (int64_t)(_end - _start);
  }

  /**
    Size of the data available for reading. Returns the size of the data
    available for reading in the inuse area.

    @return bytes available for reading from the inuse area.
    对于读操作可以读取的长度。与 size() 等价
  */
  int64_t
  read_avail() const
  {
    return (int64_t)(_end - _start);
  }

  /**
    Space available in the buffer. Returns the number of bytes that can
    be written to the data buffer.

    @return space available for writing in this IOBufferBlock.
    对于写操作可以继续写入的长度，表示缓冲区中可用的空间大小
  */
  int64_t
  write_avail()
  {
    return (int64_t)(_buf_end - _end);
  }

  /**
    Size of the memory allocated by the underlying IOBufferData.
    Computes the size of the entire block, which includes the used and
    available areas. It is the memory allocated by the IOBufferData
    referenced by this IOBufferBlock.
    由底层 IOBufferData 分配的内存大小。计算整个块的大小，包括已用的和可用的空间。
    它是由该 IOBufferBlock 引用的 IOBufferData 分配的内存.

    @return bytes allocated to the IOBufferData referenced by this
      IOBufferBlock.

    返回底层数据块的大小，调用 IOBufferData->block_size() 方法
  */
  int64_t
  block_size()
  {
    return data->block_size();
  }

  /**
    Decrease the size of the inuse area. Moves forward the start of
    the inuse area. This also decreases the number of available bytes
    for reading.

    @param len bytes to consume or positions to skip for the start of
      the inuse area.
    自 IOBufferBlock 缓冲区的头部消费一定字节的数据，_start += len 
    应用程序自 IOBufferBlock 缓冲区的头部将数据消费后，调用此方法来标记这部分
    缓冲区的内存空间可以被回收.
  */
  void consume(int64_t len);

  /**
    Increase the inuse area of the block. Adds 'len' bytes to the inuse
    area of the block. Data should be copied into the data buffer by
    using end() to find the start of the free space in the data buffer
    before calling fill()

    @param len bytes to increase the inuse area. It must be less than
      or equal to the value of write_avail().
    向 IOBufferBlock 缓冲区的尾部追加一定字节的数据，_end += len
    应用程序将数据追加到 IOBufferBlock 缓冲区的尾部后，调用此方法来标记缓冲区中
    实际可读取数据的存量。
    首先，要用 end() 获取当前的数据区域的结尾，然后拷贝数据到结尾后，再调用此方法
    注意，拷贝数据的长度 len <= write_avail()
  */
  void fill(int64_t len);

  /**
    Reset the inuse area. The start and end of the inuse area are reset
    but the actual IOBufferData referenced by this IOBufferBlock is not
    modified.  This effectively reduces the number of bytes available
    for reading to zero, and the number of bytes available for writing
    to the size of the entire buffer.
    重置正在使用的数据区域。正在使用的数据区域的起始和结束将会被重置，但由该
    IOBufferBlock 引用的实际 IOBufferData 不会被修改。这将有效地将可用于读的字节数
    减少到 0，将可用于写的字节数减少到整个缓冲区大小。

    重置正在使用的数据区域，_start = _end = buf()，_buf_end = buf() + block_size()
    重置之后，read_avail() == 0, write_avail() == block_size()
  */
  void reset();

  /**
    Create a copy of the IOBufferBlock. Creates and returns a copy of this
    IOBufferBlock that references the same data that this IOBufferBlock
    (it does not allocate an another buffer). The cloned block will not
    have a writable space since the original IOBufferBlock mantains the
    ownership for writing data to the block.
    生成 IOBufferBlock 的副本。创建并返回该 IOBufferBlock 的副本，该副本引用与此
    IOBufferBlock 相同的数据（也就是说不会分配另一个缓冲区）。该克隆块没有可写的空间，
    因为原始的 IOBlockBlock 保留了将数据写入到块中的所有权.

    @return copy of this IOBufferBlock.
    注，克隆出来的 IOBufferBlock 实例的 write_avail() == 0，就是 buf_end = end
  */
  IOBufferBlock *clone() const;

  /**
    Clear the IOBufferData this IOBufferBlock handles. Clears this
    IOBufferBlock's reference to the data buffer (IOBufferData). You can
    use alloc after this call to allocate an IOBufferData associated to
    this IOBufferBlock.
    清除该 IOBufferBlock 引用的数据缓冲区（IOBufferData）。可以在调用该函数后
    使用 alloc 分配一个与该 IOBufferBlock 关联的 IOBufferData.

    清除底层数据块，注意与 reset() 的区别
    本操作通过 data = NULL 只断开当前 block 与底层数据块的连接，底层数据块是否被释放/回收，
    由其引用计数决定。由于 IOBufferBlock 是一个链表，因此要递归将 next 的引用计数减少，如果
    减少为 0 时，还要调用 free() 释放 next 指向的 block。
    最后，data = _buf_end = _end = _start = next = NULL
    事实上可以把 clear 看做析构函数，除了不释放 block 自身
    如果需要重新使用这个 block，可以通过 alloc 重新分配底层数据块
  */
  void clear();

  /**
    Allocate a data buffer. Allocates a data buffer for this IOBufferBlock
    based on index 'i'.  Index values are described in the remarks
    section in MIOBuffer.
    根据索引 'i' 为该 IOBufferBlock 分配一个数据缓冲区。索引值在 MIOBuffer
    的 remark section 做了说明.

    分配一块长度索引值为 i 的 buffer 给 data，并通过 reset() 方法初始化
  */
  void alloc(int64_t i = default_large_iobuffer_size);

  /**
    Clear the IOBufferData this IOBufferBlock handles. Clears this
    IOBufferBlock's reference to the data buffer (IOBufferData).
    清除该 IOBufferBlock 引用的数据缓冲区（IOBufferData）。
    直接调用 clear() 方法.
    
  */
  void dealloc();

  /**
    Set or replace this IOBufferBlock's IOBufferData member. Sets this
    IOBufferBlock's IOBufferData member to point to the IOBufferData
    passed in. You can optionally specify the inuse area with the 'len'
    argument and an offset for the start.
    设置或替换该 IOBufferBlock 的 IOBufferData 成员。设置该 IOBufferBlock
    的 IOBufferData 成员指向传入的 IOBufferData。可以选择使用 'len' 参数指定
    正在使用的数据区域，并为 start 指定 offset.

    @param d new IOBufferData this IOBufferBlock references.
    @param len in use area to set. It must be less than or equal to the
      length of the block size *IOBufferData).
    @param offset bytes to skip from the beginning of the IOBufferData
      and to mark its start.

  */
  void set(IOBufferData *d, int64_t len = 0, int64_t offset = 0);
  /* 通过内部调用建立一个没有立即分配内存块的 IOBufferData 实例
   * 然后将已经分配好的内存指针赋值给 IOBufferData 实例，其它与 set 相同
   */
  void set_internal(void *b, int64_t len, int64_t asize_index);
  /* 把当前数据复制到 b，然后调用 dealloc() 释放数据块，然后调用 set_internal()
   * 最后让新数据块的 size() 与原数据块一致: _end = _start + old_size
   */
  void realloc_set_internal(void *b, int64_t buf_size, int64_t asize_index);
  /* 同：realloc_set_internal(b, buf_size, BUFFER_SIZE_NOT_ALLOCATED) */
  void realloc(void *b, int64_t buf_size);
  /* 通过 ioBufAllocator[i].alloc_void() 来分配一个缓冲区 b，然后调用 realloc_set_internal */
  void realloc(int64_t i);
  void realloc_xmalloc(void *b, int64_t buf_size);
  void realloc_xmalloc(int64_t buf_size);

  /**
    Frees the IOBufferBlock object and its underlying memory.
    Removes the reference to the IOBufferData object and then frees
    itself. You should not use this object or reference after this
    call.
    释放 IOBufferBlock 实例自身。
    首先调用 dealloc()，然后通过 ioBlockAllocator 回收内存.

  */
  void free() override;

  /* 指向数据区内可读取的第一个字节 */
  char *_start;
  /* 指向数据区内可写入的第一个字节，也是最后一个可读取字节的下一个字节 */
  char *_end;
  /* 指向整个数据区的最后的位置，边界，此位置不能再写入数据 */
  char *_buf_end;

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  /**
    The underlying reference to the allocated memory. A reference to a
    IOBufferData representing the memory allocated to this buffer. Do
    not set or modify its value directly.

    指向 IOBufferData 类型的智能指针，上述 _start, _end, _buf_end 指针范围都落在
    其成员 _data 内，若更改 data 的指向，则必须重新设置 _start, _end, _buf_end

  */
  Ptr<IOBufferData> data;

  /**
    Reference to another IOBufferBlock. A reference to another
    IOBufferBlock that allows this object to link to other.
    形成 Block 链表，next 是指向下一个 Block 的智能指针

  */
  Ptr<IOBufferBlock> next;

  /**
    Constructor of a IOBufferBlock. Do not use it to create a new object,
    instead call new_IOBufferBlock
    IOBufferBlock 的构造函数。不要直接使用它来创建一个新的对象，而是使用
    new_IOBufferBlock 来代替.

  */
  IOBufferBlock();

  // noncopyable
  IOBufferBlock(const IOBufferBlock &) = delete;
  IOBufferBlock &operator=(const IOBufferBlock &) = delete;
};

/* 声明一个全局分配此类型实例的 ClassAllocator */
extern inkcoreapi ClassAllocator<IOBufferBlock> ioBlockAllocator;

/** A class for holding a chain of IO buffer blocks.
    This class is intended to be used as a member variable for other classes that
    need to anchor an IO Buffer chain but don't need the full @c MIOBuffer machinery.
    That is, the owner is the only reader/writer of the data.

    This does not handle incremental reads or writes well. The intent is that data is
    placed in the instance, held for a while, then used and discarded.

    @note Contrast also with @c IOBufferReader which is similar but requires an
    @c MIOBuffer as its owner.
*/
class IOBufferChain
{
  using self_type = IOBufferChain; ///< Self reference type.

public:
  /// Default constructor - construct empty chain.
  IOBufferChain() = default;
  /// Shallow copy.
  self_type &operator=(self_type const &that);

  /// Shallow append.
  self_type &operator+=(self_type const &that);

  /// Number of bytes of content.
  int64_t length() const;

  /// Copy a chain of @a blocks in to this object up to @a length bytes.
  /// If @a offset is greater than 0 that many bytes are skipped. Those bytes do not count
  /// as part of @a length.
  /// This creates a new chain using existing data blocks. This
  /// breaks the original chain so that changes there (such as appending blocks)
  /// is not reflected in this chain.
  /// @return The number of bytes written to the chain.
  int64_t write(IOBufferBlock *blocks, int64_t length, int64_t offset = 0);

  /// Add the content of a buffer block.
  /// The buffer block is unchanged.
  int64_t write(IOBufferData *data, int64_t length = 0, int64_t offset = 0);

  /// Remove @a size bytes of content from the front of the chain.
  /// @return The actual number of bytes removed.
  int64_t consume(int64_t size);

  /// Clear current chain.
  void clear();

  /// Get the first block.
  IOBufferBlock *head();
  IOBufferBlock const *head() const;

  /// STL Container support.

  /// Block iterator.
  /// @internal The reason for this is to override the increment operator.
  class const_iterator : public std::forward_iterator_tag
  {
    using self_type = const_iterator; ///< Self reference type.
  protected:
    /// Current buffer block.
    IOBufferBlock *_b = nullptr;

  public:
    using value_type = const IOBufferBlock; ///< Iterator value type.

    const_iterator() = default; ///< Default constructor.

    /// Copy constructor.
    const_iterator(self_type const &that);

    /// Assignment.
    self_type &operator=(self_type const &that);

    /// Equality.
    bool operator==(self_type const &that) const;
    /// Inequality.
    bool operator!=(self_type const &that) const;

    value_type &operator*() const;
    value_type *operator->() const;

    self_type &operator++();
    self_type operator++(int);
  };

  class iterator : public const_iterator
  {
    using self_type = iterator; ///< Self reference type.
  public:
    using value_type = IOBufferBlock; ///< Dereferenced type.

    value_type &operator*() const;
    value_type *operator->() const;
  };

  using value_type = IOBufferBlock;

  iterator begin();
  const_iterator begin() const;

  iterator end();
  const_iterator end() const;

protected:
  /// Append @a block.
  void append(IOBufferBlock *block);

  /// Head of buffer block chain.
  Ptr<IOBufferBlock> _head;
  /// Tail of the block chain.
  IOBufferBlock *_tail = nullptr;
  /// The amount of data of interest.
  /// Not necessarily the amount of data in the chain of blocks.
  int64_t _len = 0;
};

/**
  An independent reader from an MIOBuffer. A reader for a set of
  IOBufferBlocks. The IOBufferReader represents the place where a given
  consumer of buffer data is reading from. It provides a uniform interface
  for easily accessing the data contained in a list of IOBufferBlocks
  associated with the IOBufferReader.

  IOBufferReaders are the abstraction that determine when data blocks
  can be removed from the buffer.


  IOBufferReader
  - 不依赖 MIOBuffer
      - 当多个读取者从同一个 MIOBuffer 中读取数据时，每一个读取者都需要标记
        自己从哪里开始读，总共读多少数据，当前读了多少
      - 此时不能直接修改 MIOBuffer 中的指针，而通过 IOBufferReader 来描述这些元素
  - 用于读取一组 IOBufferBlock
      - 通过 MIOBuffer 来创建 IOBufferReader 时，是直接从 MIOBuffer 中复制 IOBufferBlock 的成员
      - 因此实际上是对 IOBufferBlock 进行读取操作
  - IOBufferReader 表示一个给定的缓冲区数据的消费者从哪儿开始读取数据
  - 提供了一个统一的界面，可以轻松访问一组 IOBufferBlock 内包含的数据
  - IOBufferReader 内部封装了自动移除数据块的判断逻辑
      - consume 方法中 block = block->next 的操作会导致 block 的引用计数变化，通过自动指针 Ptr 实现
        对 Block 和 Data 的自动释放
  - 简单的说：IOBufferReader 将多个 IOBufferBlock 单链表作为一个大的缓冲区，提供了读取/消费这个缓冲区
    的一组方法
  - 内部成员 智能指针 block 指向多个 IOBufferBlock 构成的单链表的第一个元素
  - 内部成员 mbuf 指回到创建此 IOBufferReader 实例的 MIOBuffer 实例

  看上去 IOBufferReader 是可以直接访问 IOBufferBlock 链表的，但是其内部成员 mbuf 又决定了它是为
  MIOBuffer 设计的

*/
class IOBufferReader
{
public:
  /**
    Start of unconsumed data. Returns a pointer to first unconsumed data
    on the buffer for this reader. A null pointer indicates no data is
    available. It uses the current start_offset value.

    @return pointer to the start of the unconsumed data.

    返回可供消费（读取）的数据区域的开始位置（通过成员 start_offset 协助）
    返回 NULL 表示没有关联的数据块
  */
  char *start();

  /**
    End of inuse area of the first block with unconsumed data. Returns a
    pointer to the end of the first block with unconsumed data for this
    reader. A nullptr pointer indicates there are no blocks with unconsumed
    data for this reader.

    @return pointer to the end of the first block with unconsumed data.

    返回可供消费（读取）数据在第一个数据块（IOBufferBlock）里的结束位置
    返回 NULL 表示没有关联数据块
  */
  char *end();

  /**
    Amount of data available across all of the IOBufferBlocks. Returns the
    number of unconsumed bytes of data available to this reader across
    all remaining IOBufferBlocks. It subtracts the current start_offset
    value from the total.

    @return bytes of data available across all the buffers.

    返回当前所有数据块里面剩余可消费（读取）的数据长度
    遍历所有的数据块，累加每一个数据块内的可用数据长度再减去代表已经消费数据
    长度的 start_offset
  */
  int64_t read_avail();

  /** Check if there is more than @a size bytes available to read.
      @return @c true if more than @a size byte are available.
      返回当前所有数据块里面剩余可消费（读取）的数据长度是否大于 size
  */
  bool is_read_avail_more_than(int64_t size);

  /**
    Number of IOBufferBlocks with data in the block list. Returns the
    number of IOBufferBlocks on the block list with data remaining for
    this reader.

    @return number of blocks with data for this reader.

    返回当前所有数据块里面剩余可消费（读取）的数据块的数量
    随着数据的读取（消费）：
      成员 block 逐个指向链表的下一个 IOBufferBlock，
      成员 start_offset 也会按照新的 Block 的信息重新设置
  */
  int block_count();

  /**
    Amount of data available in the first buffer with data for this
    reader.  Returns the number of unconsumed bytes of data available
    on the first IOBufferBlock with data for this reader.

    @return number of unconsumed bytes of data available in the first
      buffer.

    返回可供消费（读取）的数据在第一个数据块里的长度
  */
  int64_t block_read_avail();

  /* 根据 start_offset 的值，跳过不需要的 block
   * start_offset 的值必须在 [0, block->size()] 范围内
   */
  void skip_empty_blocks();

  /**
    Clears all fields in this IOBuffeReader, rendering it unusable. Drops
    the reference to the IOBufferBlock list, the accesor, MIOBuffer and
    resets this reader's state. You have to set those fields in order
    to use this object again.

    清除所有成员变量，IOBufferReader 将不可用
  */
  void clear();

  /**
    Instruct the reader to reset the IOBufferBlock list. Resets the
    reader to the point to the start of the block where new data will
    be written. After this call, the start_offset field is set to zero
    and the list of IOBufferBlocks is set using the associated MIOBuffer.

    重置 IOBufferReader 的状态，成员 mbuf 和 accessor 不会被重置
    只初始化 block，start_offset，size_limit 三个成员
  */
  void reset();

  /**
    Consume a number of bytes from this reader's IOBufferBlock
    list. Advances the current position in the IOBufferBlock list of
    this reader by n bytes.

    @param n number of bytes to consume. It must be less than or equal
      to read_avail().

    记录消费 n 字节数据，n 必须小于 read_avail()
    在消费时，自动指针 block 会逐个指向 block->next
    本函数只是记录消费的状态，具体数据的读取操作，仍然要访问成员 mbuf，或者
    block 里的底层数据块来进行
  */
  void consume(int64_t n);

  /**
    Create another reader with access to the same data as this
    IOBufferReader. Allocates a new reader with the same state as this
    IOBufferReader. This means that the new reader will point to the same
    list of IOBufferBlocks and to the same buffer position as this reader.

    @return new reader with the same state as this.

    克隆当前实例，复制当前的状态和指向同样的 IOBufferBlock，以及同样的 start_offset 值
    通过直接调用 mbuf->clone_reader(this) 来实现
  */
  IOBufferReader *clone();

  /**
    Deallocate this reader. Removes and deallocates this reader from
    the underlying MIOBuffer. This IOBufferReader object must not be
    used after this call.

    释放当前实例，之后就不能再使用该实例了
    通过直接调用 mbuf->dealloc_reader(this) 来实现
  */
  void dealloc();

  /**
    Get a pointer to the first block with data. Returns a pointer to
    the first IOBufferBlock in the block chain with data available for
    this reader

    @return pointer to the first IOBufferBlock in the list with data
      available for this reader.

    返回 block 链表中保存有可用数据的第一个 IOBufferBlock
  */
  IOBufferBlock *get_current_block();

  /**
    Consult this reader's MIOBuffer writable space. Queries the MIOBuffer
    associated with this reader about the amount of writable space
    available without adding any blocks on the buffer and returns true
    if it is less than the water mark.

    @return true if the MIOBuffer associated with this IOBufferReader
      returns true in MIOBuffer::current_low_water().

    当前剩余可写入空间是否处于底限状态
    直接调用 mbuf->current_low_water() 实现
    判断规则如下：
      在不增加 block 的情况下，当前 MIOBuffer 类型的成员 mbuf 可写入的空间，
      如果低于 water_mark 值，则返回 true，否则返回 false
  */
  bool current_low_water();

  /**
    Queries the underlying MIOBuffer about. Returns true if the amount
    of writable space after adding a block on the underlying MIOBuffer
    is less than its water mark. This function call may add blocks to
    the MIOBuffer (see MIOBuffer::low_water()).

    @return result of MIOBuffer::low_water() on the MIOBuffer for
      this reader.

    当前剩余空间是否低于低限状态
    如果底层的 MIOBuffer 的可写空间在添加一个 block 后仍然小于 water_mark，
    则返回 true
  */
  bool low_water();

  /**
    To see if the amount of data available to the reader is greater than
    the MIOBuffer's water mark. Indicates whether the amount of data
    available to this reader exceeds the water mark for this reader's
    MIOBuffer.

    @return true if the amount of data exceeds the MIOBuffer's water mark.

    当前可读数据是否高于 water_mark
  */
  bool high_water();

  /**
    Perform a memchr() across the list of IOBufferBlocks. Returns the
    offset from the current start point of the reader to the first
    occurence of character 'c' in the buffer.
    在 IOBufferBlock 链表中执行 memchr()。返回从 reader 的当前起始点到
    缓冲区中第一次出现字符 'c' 的偏移量.

    @param c character to look for.
    @param len number of characters to check. If len exceeds the number
      of bytes available on the buffer or INT64_MAX is passed in, the
      number of bytes available to the reader is used. It is independent
      of the offset value.
    @param offset number of the bytes to skip over before beginning
      the operation.
    @return -1 if c is not found, otherwise position of the first
      ocurrence.

    在 IOBufferBlock 链表上执行 memchr 操作
    返回 -1 表示没有找到 c，否则返回 c 首次在 IOBufferBlock 链表中出现的偏移值
  */
  inkcoreapi int64_t memchr(char c, int64_t len = INT64_MAX, int64_t offset = 0);

  /**
    Copies and consumes data. Copies len bytes of data from the buffer
    into the supplied buffer, which must be allocated prior to the call
    and it must be at large enough for the requested bytes. Once the
    data is copied, it consumed from the reader.

    @param buf in which to place the data.
    @param len bytes to copy and consume. If 'len' exceeds the bytes
      available to the reader, the number of bytes available is used
      instead.

    @return number of bytes copied and consumed.

    从当前的 IOBufferBlock 链表，复制 len 长度的数据到 buf，buf 必须事先分配好空间
    如果 len 超过了当前可读取的数据长度，则使用当前可读取数据的长度作为 len 值
    通过 consume() 消费已经读取完成的 block
    返回当前实际复制的数据长度
  */
  inkcoreapi int64_t read(void *buf, int64_t len);

  /**
    Copy data but do not consume it. Copies 'len' bytes of data from
    the current buffer into the supplied buffer. The copy skips the
    number of bytes specified by 'offset' beyond the current point of
    the reader. It also takes into account the current start_offset value.

    @param buf in which to place the data. The pointer is modified after
      the call and points one position after the end of the data copied.
    @param len bytes to copy. If len exceeds the bytes available to the
      reader or INT64_MAX is passed in, the number of bytes available is
      used instead. No data is consumed from the reader in this operation.
    @param offset bytes to skip from the current position. The parameter
      is modified after the call.
    @return pointer to one position after the end of the data copied. The
      parameter buf is set to this value also.

    从当前的 IOBufferBlock 链表的当前位置，偏移 offset 字节开始，复制 len 长度的
    数据到 buf。但是与 read() 不同，该操作不执行 consume()
    返回指针，指向 memcpy 向 buf 中写入数据的结尾；如果没有发生写操作，则等于 buf。
    例如，当 offset 超过当前可读数据的最大值，返回值就等于 buf
  */
  inkcoreapi char *memcpy(const void *buf, int64_t len = INT64_MAX, int64_t offset = 0);

  /**
    Subscript operator. Returns a reference to the character at the
    specified position. You must ensure that it is within an appropriate
    range.
    下标操作。返回指定位置的相关字符。必须确保在合理的范围内.

    @param i positions beyond the current point of the reader. It must
      be less than the number of the bytes available to the reader.

    @return reference to the character in that position.

    重载下标操作符 reader[i]，可以像使用数组一样使用 IOBufferBlock
    使用时需要注意不要让 i 超出界限，否则会抛出异常
    这里没有使用 const char 来声明返回值的类型，但是我们仍然应该只从 IOBufferReader 中
    读取数据，虽然没有硬性限制不可向其写入，但是实际上这里不应该使用 reader[i] = x; 的代码
  */
  char &operator[](int64_t i);

  /* 返回成员 mbuf */
  MIOBuffer *
  writer() const
  {
    return mbuf;
  }
  /* 
   * 当一个 IOBufferReader 已经与一个 MIOBuffer 关联之后，就会设置 mbuf 指向该 MIOBuffer
   * 通过这个方法来判断当前的 IOBufferReader 是否已经与 MIOBuffer 关联
   * 返回与之关联的 MIOBuffer 指针，或 NULL 表示未关联
   */
  MIOBuffer *
  allocated() const
  {
    return mbuf;
  }

  /* 如果与 MIOBufferAccessor 关联，则指向 MIOBufferAccessor */
  MIOBufferAccessor *accessor; // pointer back to the accessor

  /**
    Back pointer to this object's MIOBuffer. A pointer back to the
    MIOBuffer this reader is allocated from.

    指向分配此 IOBufferReader 的 MIOBuffer
  */
  MIOBuffer *mbuf;
  /* 智能指针 block 在初始情况指向 MIOBuffer 里的 IOBufferBlock 的链表头
   * 随着 consume() 的使用，block 逐个指向 block->next */
  Ptr<IOBufferBlock> block;

  /**
    Offset beyond the shared start(). The start_offset is used in the
    calls that copy or consume data and is an offset at the beginning
    of the available data.

    start_offset 用来标记当前可用数据与 block 成员的偏移位置
    每次 block = block->next，start_offset 都会重新计算
    通常情况下 start_offset 不会大于当前 block 的可用数据长度
    如果超过，则在 skip_empty_blocks() 中会跳过无用的 block 并修正该值
  */
  int64_t start_offset;
  int64_t size_limit;

  /* 构造函数 */
  IOBufferReader() : accessor(nullptr), mbuf(nullptr), start_offset(0), size_limit(INT64_MAX) {}
};

/**
  A multiple reader, single writer memory buffer. MIOBuffers are at
  the center of all IOCore data transfer. MIOBuffers are the data
  buffers used to transfer data to and from VConnections. A MIOBuffer
  points to a list of IOBufferBlocks which in turn point to IOBufferData
  structures that in turn point to the actual data. MIOBuffer allows one
  producer and multiple consumers. The buffer fills up according the
  amount of data outstanding for the slowest consumer. Thus, MIOBuffer
  implements automatic flow control between readers of different speeds.
  Data on IOBuffer is immutable. Once written it cannot be modified, only
  deallocated once all consumers have finished with it. Immutability is
  necessary since data can be shared between buffers, which means that
  multiple IOBufferBlock objects may reference the same data but only
  one will have ownership for writing.

  多个 reader，单个 writer 的内存缓存区。MIOBuffer 位于所有 IOCore 数据传输的中心，
  MIOBuffer 是用于在 VConnections 之间传输数据的数据缓冲区。MIOBuffer 指向 
  IOBufferBlock 列表，而 IOBufferBlock 指向 IOBufferData 结构，IOBufferData
  又指向实际的数据。MIOBuffer 允许一个生产者和多个消费者。缓冲区根据最慢的 reader
  未完成的数据量来填充。因此，MIOBuffer 在不同速率的 reader 之间实现自动流量控制。
  IOBuffer 上的数据是不可变的。一旦写入就不能修改，只有在所有的消费者都使用完它之后
  才可以解除分配。由于数据可以在缓冲区之间共享，因此不可变性是必要的，这意味着多个
  IOBufferBlock 对象可以引用相同的数据，但只有一个具有写入的所有权。

  MIOBuffer:
  - 它是一个单一写（生产者），多重读（消费者）的内存缓冲区
  - 是所有 IOCore 数据传输的中心
  - 它是用于 VConnection 接收和发送操作的数据缓冲区
  - 它指向一组 IOBufferBlock，IOBufferBlock 又指向包含实际数据的 IOBufferData 结构
  - MIOBuffer 允许一个生产者和多个消费者
  - 它写入（生产）数据的速度取决于最慢的那个数据读取（消费）者
  - 它支持多个不同速度的读取（消费）者之间的自动流量控制
  - IOBuffer 内的数据是不可改变的，一旦写入就不能修改
  - 在所有的读取（消费）完成后，一次性释放
  - 由于数据（IOBufferData）可以在 buffer 之间共享
      - 多个 IOBufferBlock 就可能引用同一个数据（IOBufferData）
      - 但是只有一个具有所有权，可以写入数据，如果允许修改 IOBuffer 内的数据，就会导致混乱
  - 成员 IOBufferReader readers[MAX_MIOBUFFER_READERS] 定义了多重读（消费者），默认最大值 5
  - 可以通过 new_MIOBuffer() 创建一个实例，free_MIOBuffer(mio) 销毁一个实例
  - 它的实例占用的内存空间由 ioAllocator 分配

*/
class MIOBuffer
{
public:
  /**
    Increase writer's inuse area. Instructs the writer associated with
    this MIOBuffer to increase the inuse area of the block by as much as
    'len' bytes.

    @param len number of bytes to add to the inuse area of the block.

    将当前正在使用的数据区域扩大 len 字节（增加量了可读数据量，减少了可写空间）
    直接对 IOBufferBlock 链表 _writer 成员进行操作，如果当前剩余的可写空间不足，会自动追加空的 block
  */
  void fill(int64_t len);

  /**
    Adds a block to the end of the block list. The block added to list
    must be writable by this buffer and must not be writable by any
    other buffer.

    向 IOBufferBlock 链表 _writer 成员追加 block
    Block *b 必须是当前 MIOBuffer 可写，其他 MIOBuffer 不可写
  */
  void append_block(IOBufferBlock *b);

  /**
    Adds a new block to the end of the block list. The size is determined
    by asize_index. See the remarks section for a mapping of indexes to
    buffer block sizes.

    向 IOBufferBlock 链表 _writer 成员追加指定大小的空 block
  */
  void append_block(int64_t asize_index);

  /**
    Adds new block to the end of block list using the block size for
    the buffer specified when the buffer was allocated.

    向 IOBufferBlock 链表 _writer 成员追加当前 MIOBuffer 默认大小的空 block
    直接调用了 append_block(asize_index)
  */
  void add_block();

  /**
    Adds by reference len bytes of data pointed to by b to the end
    of the buffer.  b MUST be a pointer to the beginning of  block
    allocated from the ats_xmalloc() routine. The data will be deallocated
    by the buffer once all readers on the buffer have consumed it.

  */
  void append_xmalloced(void *b, int64_t len);

  /**
    Adds by reference len bytes of data pointed to by b to the end of the
    buffer. b MUST be a pointer to the beginning of  block allocated from
    ioBufAllocator of the corresponding index for fast_size_index. The
    data will be deallocated by the buffer once all readers on the buffer
    have consumed it.

  */
  void append_fast_allocated(void *b, int64_t len, int64_t fast_size_index);

  /**
    Adds the nbytes worth of data pointed by rbuf to the buffer. The
    data is copied into the buffer. write() does not respect watermarks
    or buffer size limits. Users of write must implement their own flow
    control. Returns the number of bytes added.

    将 rbuf 内，长度为 nbytes 字节的数据，写入 IOBufferBlock 成员 _writer 
    返回值为实际发生的写入字节数
    不检测 watermark 和 size_limit，但是剩余可写空间不足时会追加 block
    如果需要流量控制，调用者需要自己实现，本方法没有实现流量控制功能
    PS: write 是多态定义，下面还有一种实现
  */
  inkcoreapi int64_t write(const void *rbuf, int64_t nbytes);

#ifdef WRITE_AND_TRANSFER
  /**
    Same functionality as write but for the one small difference. The
    space available in the last block is taken from the original and
    this space becomes available to the copy.

    基本与下面的 write() 相同
    但是把最后一个克隆出来的 block 的写入权限从 IOBufferReader *r 引用的 MIOBuffer
    转到了当前的 MIOBuffer
    PS：每一次追加到 block 链表，_writer 成员会指向新添加的这个 block，因为 _writer 
    成员总是指向第一个可写入的 block
  */
  inkcoreapi int64_t write_and_transfer_left_over_space(IOBufferReader *r, int64_t len = INT64_MAX, int64_t offset = 0);
#endif

  /**
    Add by data from IOBufferReader r to the this buffer by reference. If
    len is INT64_MAX, all available data on the reader is added. If len is
    less than INT64_MAX, the smaller of len or the amount of data on the
    buffer is added. If offset is greater than zero, than the offset
    bytes of data at the front of the reader are skipped. Bytes skipped
    by offset reduce the number of bytes available on the reader used
    in the amount of data to add computation. write() does not respect
    watermarks or buffer size limits. Users of write must implement
    their own flow control. Returns the number of bytes added. Each
    write() call creates a new IOBufferBlock, even if it is for one
    byte. As such, it's necessary to exercise caution in any code that
    repeatedly transfers data from one buffer to another, especially if
    the data is being read over the network as it may be coming in very
    small chunks. Because deallocation of outstanding buffer blocks is
    recursive, it's possible to overrun the stack if too many blocks
    have been added to the buffer chain. It's imperative that users
    both implement their own flow control to prevent too many bytes
    from becoming outstanding on a buffer that the write() call is
    being used and that care be taken to ensure the transfers are of a
    minimum size. Should it be necessary to make a large number of small
    transfers, it's preferable to use a interface that copies the data
    rather than sharing blocks to prevent a build of blocks on the buffer.

    跳过 IOBufferReader 开始的 offset 字节的数据后，通过 clone() 复制当前 block，
    通过 append_block() 将新的 block 追加到 IOBufferBlock 成员 _writer，
    直到完成长度为 len 的数据（或剩余全部数据/INT64_MAX）的处理。
    返回值为实际发生的写入字节数
    不检测 watermark 和 size_limit，但是剩余可写空间不足时会追加 block。
    如果需要流量控制，调用者需要自己实现，本方法没有实现流量控制功能。
    即使一个 block 中可用数据只有 1 个字节，也会克隆整个 block，
    因此，当在两个 MIOBuffer 之间传输数据时需要小心处理，
    尤其是源 MIOBuffer 的数据是从网络上读取得到时，因为接收到的数据块可能都是很小的数据块。
    由于 block 的释放是一个递归过程，当有太多的 block 在链表中时，如果释放可能导致调用栈溢出
    （这里应该有错误，见上面的分析）。
    在使用 write() 方法时，建议由调用者来进行流量控制，避免链表内的 block 太多，另外要控制使用
    克隆方式传送数据的最小字节数。
    如果遇到大量小数据块的传送，建议使用第一种 write() 方法复制数据，而不是使用克隆 block 的方式。
    PS：不会修改 IOBufferReader *r 的数据消费情况.
  */
  inkcoreapi int64_t write(IOBufferReader *r, int64_t len = INT64_MAX, int64_t offset = 0);

  /** Copy data from the @a chain to this buffer.
      New IOBufferBlocks are allocated so this gets a copy of the data that is independent of the source.
      @a offset bytes are skipped at the start of the @a chain. The length is bounded by @a len and the
      size in the @a chain.

      @return the number of bytes copied.

      @internal I do not like counting @a offset against @a bytes but that's how @c write works...
  */
  int64_t write(IOBufferChain const *chain, int64_t len = INT64_MAX, int64_t offset = 0);

  /* 把 IOBufferReader *r 里面的 block 链表转移到当前 MIOBuffer
   * IOBufferReader *r 原来的 block 链表则会被清空
   * 返回所有被转移的数据长度
   * 基本上可以认为这是一个 concat 操作
   */
  int64_t remove_append(IOBufferReader *);

  /**
    Returns a pointer to the first writable block on the block chain.
    Returns nullptr if there are not currently any writable blocks on the
    block list.
    返回第一个可写的 block，
    通常 _writer 指向第一个可写的 block，
    但是极特殊情况，当前 block 写满了，那么就是 block->next 是第一个可写的 block
    如果返回 nullptr 表示没有可写的 block
  */
  IOBufferBlock *
  first_write_block()
  {
    if (_writer) {
      if (_writer->next && !_writer->write_avail()) {
        return _writer->next.get();
      }
      ink_assert(!_writer->next || !_writer->next->read_avail());
      return _writer.get();
    }

    return nullptr;
  }

  /* 返回第一个可写的 block 的 buf() */
  char *
  buf()
  {
    IOBufferBlock *b = first_write_block();
    return b ? b->buf() : nullptr;
  }

  /* 返回第一个可写的 block 的 buf_end() */
  char *
  buf_end()
  {
    return first_write_block()->buf_end();
  }

  /* 返回第一个可写的 block 的 start() */
  char *
  start()
  {
    return first_write_block()->start();
  }

  /* 返回第一个可写的 block 的 end() */
  char *
  end()
  {
    return first_write_block()->end();
  }

  /**
    Returns the amount of space of available for writing on the first
    writable block on the block chain (the one that would be reutrned
    by first_write_block()).

    返回第一个可写的 block 的剩余可写空间
  */
  int64_t block_write_avail();

  /**
    Returns the amount of space of available for writing on all writable
    blocks currently on the block chain.  Will NOT add blocks to the
    block chain.

    返回所有 block 的剩余可写空间
    此操作不会追加空的 block 到链表尾部
  */
  int64_t current_write_avail();

  /**
    Adds blocks for writing if the watermark criteria are met. Returns
    the amount of space of available for writing on all writable blocks
    on the block chain after a block due to the watermark criteria.

    返回所有 block 的剩余可写空间
    如果：可读数据小于 watermark 值 并且 剩余可写空间小于等于 watermark 值
        那么自动追加空的 block 到链表尾部
    返回值也包含了新追加的 block 的空间
  */
  int64_t write_avail();

  /**
    Returns the default data block size for this buffer.

    返回该 MIOBuffer 申请 block 时使用的缺省尺寸
    该值在创建 MIOBuffer 时传入由构造函数初始化，保存在成员 size_index 中
  */
  int64_t block_size();

  /**
    Returns the default data block size for this buffer.

  */
  int64_t
  total_size()
  {
    return block_size();
  }

  /**
    Returns true if amount of the data outstanding on the buffer exceeds
    the watermark.

    可读数据长度超过 watermark 值时返回 true
  */
  bool
  high_water()
  {
    return max_read_avail() > water_mark;
  }

  /**
    Returns true if the amount of writable space after adding a block on
    the buffer is less than the water mark. Since this function relies
    on write_avail() it may add blocks.

    可写空间小于等于 water_mark 值时返回 true
    此方法通过 write_avail 来获得当前剩余可写空间的大小，因此可能会追加空的 block
  */
  bool
  low_water()
  {
    return write_avail() <= water_mark;
  }

  /**
    Returns true if amount the amount writable space without adding and
    blocks on the buffer is less than the water mark.

    可写空间小于等于 water_mark 值时返回 true
    此方法不会追加空的 block
  */
  bool
  current_low_water()
  {
    return current_write_avail() <= water_mark;
  }

  /* 根据 size 的值选择一个最小的值，用来设置成员 size_index
   * 例如：
   *     当 size = 128 时，选择 size_index 为 0，此时计算出来的 block size = 128
   *     当 size = 129 时，选择 size_index 为 1，此时计算出来的 block size = 256
   */
  void set_size_index(int64_t size);

  /**
    Allocates a new IOBuffer reader and sets it's its 'accessor' field
    to point to 'anAccessor'.

    从 readers[] 成员分配一个可用的 IOBufferReader，并且设置该 Reader 的 accessor 成员指向 anAccessor
    一个 Reader 必须与 MIOBuffer 关联，如果一个 MIOBufferAccessor 与 MIOBuffer 关联，
    那么与 MIOBuffer 关联的 IOBufferReader 也必须与 MIOBufferAccessor 关联
  */
  IOBufferReader *alloc_accessor(MIOBufferAccessor *anAccessor);

  /**
    Allocates an IOBufferReader for this buffer. IOBufferReaders hold
    data on the buffer for different consumers. IOBufferReaders are
    REQUIRED when using buffer. alloc_reader() MUST ONLY be a called
    on newly allocated buffers. Calling on a buffer with data already
    placed on it will result in the reader starting at an indeterminate
    place on the buffer.

    从 readers[] 成员分配一个可用的 IOBufferReader
    与 alloc_accessor 不同，本方法将 accessor 成员指向 nullptr
    当一个 MIOBuffer 创建后，必须创建至少一个 IOBufferReader
    如果在 MIOBuffer 中填充了数据之后才创建 IOBufferReader，那么 IOBufferReader 可能无法从数据的开头进行读取。
    PS: 由于使用了智能指针 Ptr，而 _writer 又总是指向第一个可写的 block，
        那么随着 _writer 指针的移动，最早创建的 block 就会失去它的引用着，就会被 Ptr 自动释放，
        所以在创建 MIOBuffer 之后，首先就要创建一个 IOBufferReader，
        以保证最早创建的 block 被引用，这样就不会被 Ptr 自动释放了
  */
  IOBufferReader *alloc_reader();

  /**
    Allocates a new reader on this buffer and places it's starting
    point at the same place as reader r. r MUST be a pointer to a reader
    previous allocated from this buffer.

  */
  IOBufferReader *clone_reader(IOBufferReader *r);

  /**
    Deallocates reader e from this buffer. e MUST be a pointer to a reader
    previous allocated from this buffer. Reader need to allocated when a
    particularly consumer is being removed from the buffer but the buffer
    is still in use. Deallocation is not necessary when the buffer is
    being freed as all outstanding readers are automatically deallocated.

  */
  void dealloc_reader(IOBufferReader *e);

  /**
    Deallocates all outstanding readers on the buffer.

  */
  void dealloc_all_readers();

  void set(void *b, int64_t len);
  void set_xmalloced(void *b, int64_t len);
  void alloc(int64_t i = default_large_iobuffer_size);
  void alloc_xmalloc(int64_t buf_size);
  void append_block_internal(IOBufferBlock *b);
  int64_t write(IOBufferBlock const *b, int64_t len, int64_t offset);
  int64_t puts(char *buf, int64_t len);

  // internal interface

  bool
  empty()
  {
    return !_writer;
  }
  int64_t max_read_avail();

  int max_block_count();
  void check_add_block();

  IOBufferBlock *get_current_block();

  void
  reset()
  {
    if (_writer) {
      _writer->reset();
    }
    for (auto &reader : readers) {
      if (reader.allocated()) {
        reader.reset();
      }
    }
  }

  void
  init_readers()
  {
    for (auto &reader : readers) {
      if (reader.allocated() && !reader.block) {
        reader.block = _writer;
      }
    }
  }

  void
  dealloc()
  {
    _writer = nullptr;
    dealloc_all_readers();
  }

  void
  clear()
  {
    dealloc();
    size_index = BUFFER_SIZE_NOT_ALLOCATED;
    water_mark = 0;
  }

  void
  realloc(int64_t i)
  {
    _writer->realloc(i);
  }
  void
  realloc(void *b, int64_t buf_size)
  {
    _writer->realloc(b, buf_size);
  }
  void
  realloc_xmalloc(void *b, int64_t buf_size)
  {
    _writer->realloc_xmalloc(b, buf_size);
  }
  void
  realloc_xmalloc(int64_t buf_size)
  {
    _writer->realloc_xmalloc(buf_size);
  }

  int64_t size_index;

  /**
    Determines when to stop writing or reading. The watermark is the
    level to which the producer (filler) is required to fill the buffer
    before it can expect the reader to consume any data.  A watermark
    of zero means that the reader will consume any amount of data,
    no matter how small.

  */
  int64_t water_mark;

  /* 指向 IOBufferBlock 链表的第一个节点，每次通过 write 方法向 MIOBuffer 里写入数据时，
   * 就会写入这个链表，当一个节点写满了，就会再增加一个节点。当我们通过 IOBufferReader 
   * 消费数据时，对于已经消费掉的节点，Ptr 指针自动调用该节点的 free() 方法释放该节点所
   * 占用的空间 */
  Ptr<IOBufferBlock> _writer;
  IOBufferReader readers[MAX_MIOBUFFER_READERS];

#ifdef TRACK_BUFFER_USER
  const char *_location;
#endif

  MIOBuffer(void *b, int64_t bufsize, int64_t aWater_mark);
  MIOBuffer(int64_t default_size_index);
  MIOBuffer();
  ~MIOBuffer();
};

/**
  A wrapper for either a reader or a writer of an MIOBuffer.

*/
struct MIOBufferAccessor {
  IOBufferReader *
  reader()
  {
    return entry;
  }

  MIOBuffer *
  writer()
  {
    return mbuf;
  }

  int64_t
  block_size() const
  {
    return mbuf->block_size();
  }

  int64_t
  total_size() const
  {
    return block_size();
  }

  void reader_for(IOBufferReader *abuf);
  void reader_for(MIOBuffer *abuf);
  void writer_for(MIOBuffer *abuf);

  void
  clear()
  {
    mbuf  = nullptr;
    entry = nullptr;
  }

  MIOBufferAccessor()
    :
#ifdef DEBUG
      name(nullptr),
#endif
      mbuf(nullptr),
      entry(nullptr)
  {
  }

  ~MIOBufferAccessor();

#ifdef DEBUG
  const char *name;
#endif

  // noncopyable
  MIOBufferAccessor(const MIOBufferAccessor &) = delete;
  MIOBufferAccessor &operator=(const MIOBufferAccessor &) = delete;

private:
  MIOBuffer *mbuf;
  IOBufferReader *entry;
};

extern MIOBuffer *new_MIOBuffer_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  int64_t size_index = default_large_iobuffer_size);

#ifdef TRACK_BUFFER_USER
class MIOBuffer_tracker
{
  const char *loc;

public:
  MIOBuffer_tracker(const char *_loc) : loc(_loc) {}
  MIOBuffer *
  operator()(int64_t size_index = default_large_iobuffer_size)
  {
    return new_MIOBuffer_internal(loc, size_index);
  }
};
#endif

extern MIOBuffer *new_empty_MIOBuffer_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  int64_t size_index = default_large_iobuffer_size);

#ifdef TRACK_BUFFER_USER
class Empty_MIOBuffer_tracker
{
  const char *loc;

public:
  Empty_MIOBuffer_tracker(const char *_loc) : loc(_loc) {}
  MIOBuffer *
  operator()(int64_t size_index = default_large_iobuffer_size)
  {
    return new_empty_MIOBuffer_internal(loc, size_index);
  }
};
#endif

/// MIOBuffer allocator/deallocator
#ifdef TRACK_BUFFER_USER
#define new_MIOBuffer MIOBuffer_tracker(RES_PATH("memory/IOBuffer/"))
#define new_empty_MIOBuffer Empty_MIOBuffer_tracker(RES_PATH("memory/IOBuffer/"))
#else
#define new_MIOBuffer new_MIOBuffer_internal
#define new_empty_MIOBuffer new_empty_MIOBuffer_internal
#endif
extern void free_MIOBuffer(MIOBuffer *mio);
//////////////////////////////////////////////////////////////////////

extern IOBufferBlock *new_IOBufferBlock_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc
#endif
);

extern IOBufferBlock *new_IOBufferBlock_internal(
#ifdef TRACK_BUFFER_USER
  const char *loc,
#endif
  IOBufferData *d, int64_t len = 0, int64_t offset = 0);

#ifdef TRACK_BUFFER_USER
class IOBufferBlock_tracker
{
  const char *loc;

public:
  IOBufferBlock_tracker(const char *_loc) : loc(_loc) {}
  IOBufferBlock *
  operator()()
  {
    return new_IOBufferBlock_internal(loc);
  }
  IOBufferBlock *
  operator()(Ptr<IOBufferData> &d, int64_t len = 0, int64_t offset = 0)
  {
    return new_IOBufferBlock_internal(loc, d.get(), len, offset);
  }
};
#endif

/// IOBufferBlock allocator
#ifdef TRACK_BUFFER_USER
#define new_IOBufferBlock IOBufferBlock_tracker(RES_PATH("memory/IOBuffer/"))
#else
#define new_IOBufferBlock new_IOBufferBlock_internal
#endif
////////////////////////////////////////////////////////////

extern IOBufferData *new_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *location,
#endif
  int64_t size_index = default_large_iobuffer_size, AllocType type = DEFAULT_ALLOC);

extern IOBufferData *new_xmalloc_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *location,
#endif
  void *b, int64_t size);

extern IOBufferData *new_constant_IOBufferData_internal(
#ifdef TRACK_BUFFER_USER
  const char *locaction,
#endif
  void *b, int64_t size);

#ifdef TRACK_BUFFER_USER
class IOBufferData_tracker
{
  const char *loc;

public:
  IOBufferData_tracker(const char *_loc) : loc(_loc) {}
  IOBufferData *
  operator()(int64_t size_index = default_large_iobuffer_size, AllocType type = DEFAULT_ALLOC)
  {
    return new_IOBufferData_internal(loc, size_index, type);
  }
};
#endif

#ifdef TRACK_BUFFER_USER
#define new_IOBufferData IOBufferData_tracker(RES_PATH("memory/IOBuffer/"))
#define new_xmalloc_IOBufferData(b, size) new_xmalloc_IOBufferData_internal(RES_PATH("memory/IOBuffer/"), (b), (size))
#define new_constant_IOBufferData(b, size) new_constant_IOBufferData_internal(RES_PATH("memory/IOBuffer/"), (b), (size))
#else
#define new_IOBufferData new_IOBufferData_internal
#define new_xmalloc_IOBufferData new_xmalloc_IOBufferData_internal
#define new_constant_IOBufferData new_constant_IOBufferData_internal
#endif

extern int64_t iobuffer_size_to_index(int64_t size, int64_t max = max_iobuffer_size);
extern int64_t index_to_buffer_size(int64_t idx);
/**
  Clone a IOBufferBlock chain. Used to snarf a IOBufferBlock chain
  w/o copy.

  @param b head of source IOBufferBlock chain.
  @param offset # bytes in the beginning to skip.
  @param len bytes to copy from source.
  @return ptr to head of new IOBufferBlock chain.

*/
extern IOBufferBlock *iobufferblock_clone(IOBufferBlock *b, int64_t offset, int64_t len);
/**
  Skip over specified bytes in chain. Used for dropping references.

  @param b head of source IOBufferBlock chain.
  @param poffset originally offset in b, finally offset in returned
    IOBufferBlock.
  @param plen value of write is subtracted from plen in the function.
  @param write bytes to skip.
  @return ptr to head of new IOBufferBlock chain.

*/
extern IOBufferBlock *iobufferblock_skip(IOBufferBlock *b, int64_t *poffset, int64_t *plen, int64_t write);

inline IOBufferChain &
IOBufferChain::operator=(self_type const &that)
{
  _head = that._head;
  _tail = that._tail;
  _len  = that._len;
  return *this;
}

inline IOBufferChain &
IOBufferChain::operator+=(self_type const &that)
{
  if (nullptr == _head)
    *this = that;
  else {
    _tail->next = that._head;
    _tail       = that._tail;
    _len += that._len;
  }
  return *this;
}

inline int64_t
IOBufferChain::length() const
{
  return _len;
}

inline IOBufferBlock const *
IOBufferChain::head() const
{
  return _head.get();
}

inline IOBufferBlock *
IOBufferChain::head()
{
  return _head.get();
}

inline void
IOBufferChain::clear()
{
  _head = nullptr;
  _tail = nullptr;
  _len  = 0;
}

inline IOBufferChain::const_iterator::const_iterator(self_type const &that) : _b(that._b) {}

inline IOBufferChain::const_iterator &
IOBufferChain::const_iterator::operator=(self_type const &that)
{
  _b = that._b;
  return *this;
}

inline bool
IOBufferChain::const_iterator::operator==(self_type const &that) const
{
  return _b == that._b;
}

inline bool
IOBufferChain::const_iterator::operator!=(self_type const &that) const
{
  return _b != that._b;
}

inline IOBufferChain::const_iterator::value_type &IOBufferChain::const_iterator::operator*() const
{
  return *_b;
}

inline IOBufferChain::const_iterator::value_type *IOBufferChain::const_iterator::operator->() const
{
  return _b;
}

inline IOBufferChain::const_iterator &
IOBufferChain::const_iterator::operator++()
{
  _b = _b->next.get();
  return *this;
}

inline IOBufferChain::const_iterator
IOBufferChain::const_iterator::operator++(int)
{
  self_type pre{*this};
  ++*this;
  return pre;
}

inline IOBufferChain::iterator::value_type &IOBufferChain::iterator::operator*() const
{
  return *_b;
}

inline IOBufferChain::iterator::value_type *IOBufferChain::iterator::operator->() const
{
  return _b;
}
