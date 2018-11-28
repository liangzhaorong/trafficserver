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
#define I_VIO_h

#include "tscore/ink_platform.h"
#include "I_EventSystem.h"
#if !defined(I_IOBuffer_h)
#error "include I_IOBuffer.h"
---include I_IOBuffer.h
#endif
#include "tscore/ink_apidefs.h"
   class Continuation;
class VConnection;
class IOVConnection;
class MIOBuffer;
class ProxyMutex;

/**
  Descriptor for an IO operation.
  描述一个 IO 操作.

  A VIO is a descriptor for an in progress IO operation. It is
  returned from do_io_read() and do_io_write() methods on VConnections.
  Through the VIO, the state machine can monitor the progress of
  an operation and reenable the operation when data becomes available.
  VIO 描述一个正在进行的 IO 操作。它是由 VConnections 里的 do_io_read() 和
  do_io_write() 方法返回。通过 VIO，状态机可以监控到操作的进展，在数据到达时
  可以激活操作。

  The VIO operation represents several types of operations, and
  they can be identified through the 'op' member. It can take any
  of the following values:

  <table>
    <tr>
      <td align="center"><b>Constant</b></td>
      <td align="center"><b>Meaning</b></td>
    </tr>
    <tr><td>READ</td><td>The VIO represents a read operation</td></tr>
    <tr><td>WRITE</td><td>The VIO represents a write operation</td></tr>
    <tr><td>CLOSE</td><td>The VIO represents the request to close the
                          VConnection</td></tr>
    <tr><td>ABORT</td><td></td></tr>
    <tr><td>SHUTDOWN_READ</td><td></td></tr>
    <tr><td>SHUTDOWN_WRITE</td><td></td></tr>
    <tr><td>SHUTDOWN_READWRITE</td><td></td></tr>
    <tr><td>SEEK</td><td></td></tr>
    <tr><td>PREAD</td><td></td></tr>
    <tr><td>PWRITE</td><td></td></tr>
    <tr><td>STAT</td><td></td></tr>
  </table>

*/
/*
 * 理解 VIO
 * 
 * 状态机 SM 需要发起 IO 操作时，通过创建 VIO 来告知 IOCore，IOCore 在执行了物理 IO 操作后再通过 VIO 回调
 * （通知）状态机 SM。所以 VIO 中包含了三个元素：BufferAccessor，VConnection，Cont(SM)
 * 数据流在 BufferAccessor 与 VConnection 之间流动，消息流在 IOCore 与 Cont(SM)之间传递。
 * 需要注意的是 VIO 里的 BufferAccessor 是指向 MIOBuffer 的操作者，MIOBuffer 是由 Cont(SM)创建的。
 * 另外 IOCore 回调 Cont(SM)时，是通过 VIO 内保存的 _cont 指针进行的，不是通过 vc_server 里的 Cont
 *
 * +-------+                 +--------------------+    +-----------------------------------------+
 * |       |  +-----------+  |                    |    |  +-----------+                          |
 * |       |  |           |  |                    |    |  |           |                          |
 * |       ===> vc_server ==========> read  ==============> MIOBuffer |                          |
 * |       |  |           |  |                    |    |  |           |                          |
 * |       |  +-----------+  |                    |    |  +-----------+                          |
 * |       |                 |  vc->read.vio._cont----->handleEvent(READ_READY, &vc->read.vio)   |
 * |       |                 |                    |    |                                         |
 * |  NIC  |                 |       IOCore       |    |             VIO._Cont (SM)              |
 * |       |  +-----------+  |                    |    |  +-----------+                          |
 * |       |  |           |  |                    |    |  |           |                          |
 * |       <=== vc_server <========== write <============== MIOBuffer |                          |
 * |       |  |           |  |                    |    |  |           |                          |
 * |       |  +-----------+  |                    |    |  +-----------+                          |
 * |       |                 | vc->write.vio._cont----->handleEvent(WRITE_READY, &vc->write.vio) |
 * |       |                 |                    |    |                                         |
 * +-------+                 +--------------------+    +-----------------------------------------+
 * 对于 TCP，这里的 IOCore 指的就是 NetHandler 和 UnixNetVConnection
 * 
 * 如，当状态机（SM）想要实现接收 1M 字节数据，并且转发给客户端时，可以通过如下方式实现：
 * - 创建一个 MIOBuffer，用于存放临时接收的数据
 * - 通过 do_io_read() 在 SourceVC 上创建一个读数据的 VIO，设置读取长度为 1M 字节，并传入 MIOBuffer 用于接收临时数据
 * - IOCore 在 SourceVC 上接收到数据时会将数据生产到 VIO 中（暂存在 MIOBuffer 内）
 *   - 然后呼叫 SM->handler(EVENT_READ_READY, SourceVIO)
 *   - 在 handler 中可以对 SourceVIO 进行消费（读取 MIOBuffer 内暂存的数据），获取本次读取到的一部分数据
 * - VIO 内有一个计数器，当总生产（读取）数据量达到 1M 字节
 *   - 那么会由 IOCore 呼叫 SM->handler(EVENT_READ_COMPLETE, SourceVIO)
 *   - 在 handler 中需要首先对 SourceVIO 进行消费（读出），然后关闭 SourceVIO.
 * - 到此就完成了接收 1M 字节数据的过程
 *
 * 可以看到，ATS 通过 VIO 向 IOCore 描述一个包含若干次 IO 操作的任务，用于 VIO 操作的 MIOBuffer 可以很小，
 * 仅需要保存一次 IO 操作所需要的数据，然后由 SM 对 VIO 和 MIOBuffer 进行处理.
 * 或者，可以把 VIO 看做是一个 PIPE，一段是底层 IO 设备，一端是 MIOBuffer，一个 VIO 创建之前要选择好它的
 * 方向，创建后不可修改，如：读 VIO，就是从底层 IO 设备(vc_server)读数据到 MIOBuffer；写 VIO，就是从 MIOBuffer
 * 写数据到底层 IO 设备(vc_server)
 */
class VIO
{
public:
  ~VIO() {}
  /** Interface for the VConnection that owns this handle. */
  /* 获得与此关联的 Continuation，返回成员：cont */
  Continuation *get_continuation();
  /* 设置与此 VIO 关联的 Continuation（通常为状态机 SM），同时继承 Cont 的 mutex。
   * 通过调用：vc_server->set_continuation(this, cont) 通知到与 VIO 关联的 VConnection。
   *    - 这个 VConnection::set_continuation() 方法，随着 NT 的支持在 Yahoo 开源时被取消了，也就同时被废弃了
   * 设置之后，当此 VIO 上发生事件时，将会回调新 Cont 的 handler，并传递 Event。
   * 如果 cont == nullptr，那么将会同时清除 VIO 的 mutex，也会传递给 vc_server
   */
  void set_continuation(Continuation *cont);

  /**
    Set nbytes to be what is current available.

    Interface to set nbytes to be ndone + buffer.reader()->read_avail()
    if a reader is set.
  */
  void done();

  /**
    Determine the number of bytes remaining.
    确定剩余的字节数.

    Convenience function to determine how many bytes the operation
    has remaining.

    @return The number of bytes to be processed by the operation.

  */
  int64_t ntodo();

  /////////////////////
  // buffer settings //
  /////////////////////
  void set_writer(MIOBuffer *writer);
  void set_reader(IOBufferReader *reader);
  MIOBuffer *get_writer();
  IOBufferReader *get_reader();

  /**
    Reenable the IO operation.

    Interface that the state machine uses to reenable an I/O
    operation.  Reenable tells the VConnection that more data is
    available for the operation and that it should try to continue
    the operation in progress.  I/O operations become disabled when
    they can make no forward progress.  For a read this means that
    it's buffer is full. For a write, that it's buffer is empty.
    If reenable is called and progress is still not possible, it
    is ignored and no events are generated. However, unnecessary
    reenables (ones where no progress can be made) should be avoided
    as they hurt system throughput and waste CPU.

  */
  inkcoreapi void reenable();

  /**
    Reenable the IO operation.

    Interface that the state machine uses to reenable an I/O
    operation.  Reenable tells the VConnection that more data is
    available for the operation and that it should try to continue
    the operation in progress.  I/O operations become disabled when
    they can make no forward progress.  For a read this means that
    it's buffer is full. For a write, that it's buffer is empty.
    If reenable is called and progress is still not possible, it
    is ignored and no events are generated. However, unnecessary
    reenables (ones where no progress can be made) should be avoided
    as they hurt system throughput and waste CPU.

  */
  inkcoreapi void reenable_re();

  VIO(int aop);
  VIO();

  enum {
    NONE = 0,
    READ,
    WRITE,
    CLOSE,
    ABORT,
    SHUTDOWN_READ,
    SHUTDOWN_WRITE,
    SHUTDOWN_READWRITE,
    SEEK,
    PREAD,
    PWRITE,
    STAT,
  };

public:
  /**
    Continuation to callback.

    Used by the VConnection to store who is the continuation to
    call with events for this operation.

  */
  Continuation *cont;

  /**
    Number of bytes to be done for this operation.

    The total number of bytes this operation must complete.

  */
  int64_t nbytes;

  /**
    Number of bytes already completed.

    The number of bytes that already have been completed for the
    operation. Processor can update this value only if they hold
    the lock.

  */
  int64_t ndone;

  /**
    Type of operation.

    The type of operation that this VIO represents.

  */
  int op;

  /**
    Provides access to the reader or writer for this operation.

    Contains a pointer to the IOBufferReader if the operation is a
    write and a pointer to a MIOBuffer if the operation is a read.

  */
  MIOBufferAccessor buffer;

  /**
    Internal backpointer to the VConnection for use in the reenable
    functions.

  */
  VConnection *vc_server;

  /**
    Reference to the state machine's mutex.

    Maintains a reference to the state machine's mutex to allow
    processors to safely lock the operation even if the state machine
    has closed the VConnection and deallocated itself.

  */
  Ptr<ProxyMutex> mutex;
};

#include "I_VConnection.h"
