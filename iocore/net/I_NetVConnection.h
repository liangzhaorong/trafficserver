/** @file

  This file implements an I/O Processor for network I/O

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

#include "tscore/ink_inet.h"
#include "I_Action.h"
#include "I_VConnection.h"
#include "I_Event.h"
#include "tscore/List.h"
#include "I_IOBuffer.h"
#include "I_Socks.h"
#include "ts/apidefs.h"
#include <string_view>

#define CONNECT_SUCCESS 1
#define CONNECT_FAILURE 0

#define SSL_EVENT_SERVER 0
#define SSL_EVENT_CLIENT 1

// Indicator the context for a NetVConnection
typedef enum {
  NET_VCONNECTION_UNSET = 0,
  NET_VCONNECTION_IN,  // Client <--> ATS, Client-Side
  NET_VCONNECTION_OUT, // ATS <--> Server, Server-Side
} NetVConnectionContext_t;

/** Holds client options for NetVConnection.

    This class holds various options a user can specify for
    NetVConnection. Various clients need many slightly different
    features. This is an attempt to prevent out of control growth of
    the connection method signatures. Only options of interest need to
    be explicitly set -- the rest get sensible default values.

    @note Binding addresses is a bit complex. It is not currently
    possible to bind indiscriminately across protocols, which means
    any connection must commit to IPv4 or IPv6. For this reason the
    connection logic will look at the address family of @a local_addr
    even if @a addr_binding is @c ANY_ADDR and bind to any address in
    that protocol. If it's not an IP protocol, IPv4 will be used.
*/
struct NetVCOptions {
  typedef NetVCOptions self; ///< Self reference type.

  /// Values for valid IP protocols.
  enum ip_protocol_t {
    USE_TCP, ///< TCP protocol.
    USE_UDP  ///< UDP protocol.
  };

  /// IP (TCP or UDP) protocol to use on socket.
  ip_protocol_t ip_proto;

  /** IP address family.

      This is used for inbound connections only if @c local_ip is not
      set, which is sometimes more convenient for the client. This
      defaults to @c AF_INET so if the client sets neither this nor @c
      local_ip then IPv4 is used.

      For outbound connections this is ignored and the family of the
      remote address used.

      @note This is (inconsistently) called "domain" and "protocol" in
      other places. "family" is used here because that's what the
      standard IP data structures use.

  */
  uint16_t ip_family;

  /** The set of ways in which the local address should be bound.

      The protocol is set by the contents of @a local_addr regardless
      of this value. @c ANY_ADDR will override only the address.

      @note The difference between @c INTF_ADDR and @c FOREIGN_ADDR is
      whether transparency is enabled on the socket. It is the
      client's responsibility to set this correctly based on whether
      the address in @a local_addr is associated with an interface on
      the local system ( @c INTF_ADDR ) or is owned by a foreign
      system ( @c FOREIGN_ADDR ).  A binding style of @c ANY_ADDR
      causes the value in @a local_addr to be ignored.

      The IP address and port are separate because most clients treat
      these independently. For the same reason @c IpAddr is used
      to be clear that it contains no port data.

      @see local_addr
      @see addr_binding
   */
  enum addr_bind_style {
    ANY_ADDR,    ///< Bind to any available local address (don't care, default).
    INTF_ADDR,   ///< Bind to interface address in @a local_addr.
    FOREIGN_ADDR ///< Bind to foreign address in @a local_addr.
  };

  /** Local address for the connection.

      For outbound connections this must have the same family as the
      remote address (which is not stored in this structure). For
      inbound connections the family of this value overrides @a
      ip_family if set.

      @note Ignored if @a addr_binding is @c ANY_ADDR.
      @see addr_binding
      @see ip_family
  */
  IpAddr local_ip;
  /** Local port for connection.
      Set to 0 for "don't care" (default).
   */
  uint16_t local_port;
  /// How to bind the local address.
  /// @note Default is @c ANY_ADDR.
  addr_bind_style addr_binding;

  /// Make the socket blocking on I/O (default: @c false)
  bool f_blocking;
  /// Make socket block on connect (default: @c false)
  bool f_blocking_connect;

  // Use TCP Fast Open on this socket. The connect(2) call will be omitted.
  bool f_tcp_fastopen = false;

  /// Control use of SOCKS.
  /// Set to @c NO_SOCKS to disable use of SOCKS. Otherwise SOCKS is
  /// used if available.
  unsigned char socks_support;
  /// Version of SOCKS to use.
  unsigned char socks_version;

  int socket_recv_bufsize;
  int socket_send_bufsize;

  /// Configuration options for sockets.
  /// @note These are not identical to internal socket options but
  /// specifically defined for configuration. These are mask values
  /// and so must be powers of 2.
  uint32_t sockopt_flags;
  /// Value for TCP no delay for @c sockopt_flags.
  static uint32_t const SOCK_OPT_NO_DELAY = 1;
  /// Value for keep alive for @c sockopt_flags.
  static uint32_t const SOCK_OPT_KEEP_ALIVE = 2;
  /// Value for linger on for @c sockopt_flags
  static uint32_t const SOCK_OPT_LINGER_ON = 4;
  /// Value for TCP Fast open @c sockopt_flags
  static uint32_t const SOCK_OPT_TCP_FAST_OPEN = 8;

  uint32_t packet_mark;
  uint32_t packet_tos;

  EventType etype;

  /** Server name to use for SNI data on an outbound connection.
   */
  ats_scoped_str sni_servername;
  /** FQDN used to connect to the origin.  May be different
   * than sni_servername if pristine host headers are used
   */
  ats_scoped_str ssl_servername;

  /**
   * Client certificate to use in response to OS's certificate request
   */
  ats_scoped_str clientCertificate;
  /// Reset all values to defaults.

  uint8_t clientVerificationFlag = 0;
  void reset();

  void set_sock_param(int _recv_bufsize, int _send_bufsize, unsigned long _opt_flags, unsigned long _packet_mark = 0,
                      unsigned long _packet_tos = 0);

  NetVCOptions() { reset(); }
  ~NetVCOptions() {}
  /** Set the SNI server name.
      A local copy is made of @a name.
  */
  self &
  set_sni_servername(const char *name, size_t len)
  {
    IpEndpoint ip;

    // Literal IPv4 and IPv6 addresses are not permitted in "HostName".(rfc6066#section-3)
    if (name && len && ats_ip_pton(std::string_view(name, len), &ip) != 0) {
      sni_servername = ats_strndup(name, len);
    } else {
      sni_servername = nullptr;
    }
    return *this;
  }
  self &
  set_ssl_servername(const char *name)
  {
    if (name) {
      ssl_servername = ats_strdup(name);
    } else {
      ssl_servername = nullptr;
    }
    return *this;
  }
  self &
  set_client_certname(const char *name)
  {
    clientCertificate = ats_strdup(name);
    // clientCertificate = name;
    return *this;
  }

  self &
  operator=(self const &that)
  {
    if (&that != this) {
      /*
       * It is odd but necessary to null the scoped string pointer here
       * and then explicitly call release on them in the string assignements
       * below.
       * We a memcpy from that to this.  This will put that's string pointers into
       * this's memory.  Therefore we must first explicitly null out
       * this's original version of the string.  The release after the
       * memcpy removes the extra reference to that's copy of the string
       * Removing the release will eventualy cause a double free crash
       */
      sni_servername    = nullptr; // release any current name.
      ssl_servername    = nullptr;
      clientCertificate = nullptr;
      memcpy(static_cast<void *>(this), &that, sizeof(self));
      if (that.sni_servername) {
        sni_servername.release(); // otherwise we'll free the source string.
        this->sni_servername = ats_strdup(that.sni_servername);
      }
      if (that.ssl_servername) {
        ssl_servername.release(); // otherwise we'll free the source string.
        this->ssl_servername = ats_strdup(that.ssl_servername);
      }
      if (that.clientCertificate) {
        clientCertificate.release(); // otherwise we'll free the source string.
        this->clientCertificate = ats_strdup(that.clientCertificate);
      }
    }
    return *this;
  }

  std::string_view get_family_string() const;

  std::string_view get_proto_string() const;

  /// @name Debugging
  //@{
  /// Convert @a s to its string equivalent.
  static const char *toString(addr_bind_style s);
  //@}

  // noncopyable
  NetVCOptions(const NetVCOptions &) = delete;
};

/**
  A VConnection for a network socket. Abstraction for a net connection.
  Similar to a socket descriptor VConnections are IO handles to
  streams. In one sense, they serve a purpose similar to file
  descriptors. Unlike file descriptors, VConnections allow for a
  stream IO to be done based on a single read or write call.
  网络套接字 VConnection。网络连接的抽象。与 socket 描述符类似，VConnection
  是流的 IO 句柄。在某种意义上，它们的用途与文件描述符类似。但与文件描述符不同的是，
  VConnection 允许基于单个读/写调用完成流 IO。

*/
class NetVConnection : public AnnotatedVConnection
{
public:
  // How many bytes have been queued to the OS for sending by haven't been sent yet
  // Not all platforms support this, and if they don't we'll return -1 for them
  virtual int64_t
  outstanding()
  {
    return -1;
  };

  /**
     Initiates read. Thread safe, may be called when not handling
     an event from the NetVConnection, or the NetVConnection creation
     callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr><td>c->handleEvent(VC_EVENT_READ_READY, vio)</td><td>data added to buffer</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_READ_COMPLETE, vio)</td><td>finished reading nbytes of data</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_EOS, vio)</td><td>the stream has been shutdown</td></tr>
      <tr><td>c->handleEvent(VC_EVENT_ERROR, vio)</td><td>error</td></tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_read(). The vio can be changed only during call backs
    from the vconnection.

    @param c continuation to be called back after (partial) read
    @param nbytes no of bytes to read, if unknown set to INT64_MAX
    @param buf buffer to put the data into
    @return vio

  */
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override = 0;

  /**
    Initiates write. Thread-safe, may be called when not handling
    an event from the NetVConnection, or the NetVConnection creation
    callback.

    Callbacks: non-reentrant, c's lock taken during callbacks.

    <table>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_READY, vio)</td>
        <td>signifies data has written from the reader or there are no bytes available for the reader to write.</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_WRITE_COMPLETE, vio)</td>
        <td>signifies the amount of data indicated by nbytes has been read from the buffer</td>
      </tr>
      <tr>
        <td>c->handleEvent(VC_EVENT_ERROR, vio)</td>
        <td>signified that error occured during write.</td>
      </tr>
    </table>

    The vio returned during callbacks is the same as the one returned
    by do_io_write(). The vio can be changed only during call backs
    from the vconnection. The vconnection deallocates the reader
    when it is destroyed.

    @param c continuation to be called back after (partial) write
    @param nbytes no of bytes to write, if unknown msut be set to INT64_MAX
    @param buf source of data
    @param owner
    @return vio pointer

  */
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner = false) override = 0;

  /**
    Closes the vconnection. A state machine MUST call do_io_close()
    when it has finished with a VConenction. do_io_close() indicates
    that the VConnection can be deallocated. After a close has been
    called, the VConnection and underlying processor must NOT send
    any more events related to this VConnection to the state machine.
    Likeswise, state machine must not access the VConnectuion or
    any returned VIOs after calling close. lerrno indicates whether
    a close is a normal close or an abort. The difference between
    a normal close and an abort depends on the underlying type of
    the VConnection. Passing VIO::CLOSE for lerrno indicates a
    normal close while passing VIO::ABORT indicates an abort.

    @param lerrno VIO:CLOSE for regular close or VIO::ABORT for aborts

  */
  void do_io_close(int lerrno = -1) override = 0;

  /**
    Shuts down read side, write side, or both. do_io_shutdown() can
    be used to terminate one or both sides of the VConnection. The
    howto is one of IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE,
    IO_SHUTDOWN_READWRITE. Once a side of a VConnection is shutdown,
    no further I/O can be done on that side of the connections and
    the underlying processor MUST NOT send any further events
    (INCLUDING TIMOUT EVENTS) to the state machine. The state machine
    MUST NOT use any VIOs from a shutdown side of a connection.
    Even if both sides of a connection are shutdown, the state
    machine MUST still call do_io_close() when it wishes the
    VConnection to be deallocated.

    @param howto IO_SHUTDOWN_READ, IO_SHUTDOWN_WRITE, IO_SHUTDOWN_READWRITE

  */
  void do_io_shutdown(ShutdownHowTo_t howto) override = 0;

  /**
    Sends out of band messages over the connection. This function
    is used to send out of band messages (is this still useful?).
    cont is called back with VC_EVENT_OOB_COMPLETE - on successful
    send or VC_EVENT_EOS - if the other side has shutdown the
    connection. These callbacks could be re-entrant. Only one
    send_OOB can be in progress at any time for a VC.
    通过该连接发送带外数据。
    将长度为 len 的缓存区 buf 内的数据以带外方式发送。
    发送成功，回调 cont 时传递 VC_EVENT_OOB_COMPLETE 事件
    对方关闭，回调 cont 时传递 VC_EVENT_EOS 事件
    cont 回调必须是可重入的
    每一个 VC 同一时间只能有一个 send_OOB 操作

    @param cont to be called back with events.
    @param buf message buffer.
    @param len length of the message.

  */
  virtual Action *send_OOB(Continuation *cont, char *buf, int len);

  /**
    Cancels a scheduled send_OOB. Part of the message could have
    been sent already. Not callbacks to the cont are made after
    this call. The Action returned by send_OOB should not be accessed
    after cancel_OOB.
    取消带外数据的发送。
    取消之前调用 send_OOB 安排的带外数据发送操作，但是有可能会有部分数据已经发送出去。
    执行之后，不会再有任何对 cont 的回调，而且也不能再访问之前 send_OOB 返回的 Action 对象。
    当之前的 send_OOB 操作没有完成，而又需要进行一个新的 send_OOB 操作时，必须要执行 cancel_OOB 取消之前的操作.

  */
  virtual void cancel_OOB();

  ////////////////////////////////////////////////////////////
  // Set the timeouts associated with this connection.      //
  // active_timeout is for the total elasped time of        //
  // the connection.                                        //
  // inactivity_timeout is the elapsed time from the time   //
  // a read or a write was scheduled during which the       //
  // connection  was unable to sink/provide data.           //
  // calling these functions repeatedly resets the timeout. //
  // These functions are NOT THREAD-SAFE, and may only be   //
  // called when handing an  event from this NetVConnection,//
  // or the NetVConnection creation callback.               //
  ////////////////////////////////////////////////////////////
  /* 连接的超时设置。
   * active_timeout 用于连接的整个生存周期
   * inactivity_timeout 用于最近一次读写操作之后的生存周期.
   * 可以重复调用这些方法，以复位超时统计时间。
   * 由于这些方法不是线程安全的，所以仅可以在 NetVConnection 处理
   * 事件时调用，或者 NetVConnection 创建回调时调用。
   */

  /**
    Sets time after which SM should be notified.
    在指定时间之后通知状态机（SM）。

    Sets the amount of time (in nanoseconds) after which the state
    machine using the NetVConnection should receive a
    VC_EVENT_ACTIVE_TIMEOUT event. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. The timer is reset if the function is
    called repeatedly This call can be used by SMs to make sure
    that it does not keep any connections open for a really long
    time.
    设置一个时间量（纳秒），在此之后回调与此 NetVConnection 关联的状态机(SM)，
    并传递 VC_EVENT_ACTIVE_TIMEOUT 事件。
    如果当前连接没有处于 active 状态（没有读、写操作），那么就会忽略这个值。
    PS：事实上，是优先判断 Inactivity Timeout，当出现 Inactivity Timeout 时就不会
    回调 Active Timeout 事件。

    Timeout symantics:
    超时的定义：

    Should a timeout occur, the state machine for the read side of
    the NetVConnection is signaled first assuming that a read has
    been initiated on the NetVConnection and that the read side of
    the NetVConnection has not been shutdown. Should either of the
    two conditions not be met, the NetProcessor will attempt to
    signal the write side. If a timeout is sent to the read side
    state machine and its handler, return EVENT_DONE, a timeout
    will not be sent to the write side. Should the return from the
    handler not be EVENT_DONE and the write side state machine is
    different (in terms of pointer comparison) from the read side
    state machine, the NetProcessor will try to signal the write
    side state machine as well. To signal write side, a write must
    have been initiated on it and the write must not have been
    shutdown.
    如果超时发生，与 NetVConnection 读取端关联的状态机（SM）首先被回调，
    以确保 NetVConnection 上已经读取到了数据，并且读端没有被关闭。如果
    两个条件都没有被满足，那么 NetProcessor 尝试回调写入端关联的状态机（SM）。
    PS：读端被关闭（读半关闭，读到 0 字节），如果没有数据写入，通常意味着连接被对端关闭了。

    回调读端状态机，
        如果返回了 EVENT_DONE：
            那么就不会再回调写端状态机；
        如果返回的不是 EVENT_DONE，并且写端状态机与读端不是同一个状态机（比较回调函数的指针）：
            NetProcessor 将尝试回调写端状态机。

    回调写端之前，要确保数据已经被写入，并且写端没有关闭（写半关闭）。

    Receiving a timeout is only a notification that the timer has
    expired. The NetVConnection is still usable. Further timeouts
    of the type signaled will not be generated unless the timeout
    is reset via the set_active_timeout() or set_inactivity_timeout()
    interfaces.
    状态机收到 TIMEOUT 事件，只是一个通知，表示计时时间已到，但是     NetVConnection 仍然可用。
    除非通过 set_active_timeout() 或 set_inactivity_timeout() 重置计时器，否则后续的超时信号
    将不再被触发。

    每次调用此函数，都会重新对超时时间进行计时。
    通过这个方法，可以让状态机（SM）处理那些长时间不关闭（但是有通信）的连接。
  */
  virtual void set_active_timeout(ink_hrtime timeout_in) = 0;

  /**
    Sets time after which SM should be notified if the requested
    IO could not be performed. Sets the amount of time (in nanoseconds),
    if the NetVConnection is idle on both the read or write side,
    after which the state machine using the NetVConnection should
    receive a VC_EVENT_INACTIVITY_TIMEOUT event. Either read or
    write traffic will cause timer to be reset. Calling this function
    again also resets the timer. The timeout is value is ignored
    if neither the read side nor the write side of the connection
    is currently active. See section on timeout semantics above.
    如果发起的 IO 操作没有在指定时间完成，就通知状态机（SM）。
    如果在最近一次状态机操作 NetVConnection 之后，达到设置的时间量（纳秒），
    NetVConnection 在读端和写端仍然处于空闲状态，将会回调与此 NetVConnection 
    关联的状态机（SM），并传递 VC_EVENT_INACTIVITY_TIMEOUT 事件。

   */
  virtual void set_inactivity_timeout(ink_hrtime timeout_in) = 0;

  /**
    Clears the active timeout. No active timeouts will be sent until
    set_active_timeout() is used to reset the active timeout.
    清除 active timeout。在调用 set_active_timeout() 重新设置之前，不会再有
    active timeout 事件发送。

  */
  virtual void cancel_active_timeout() = 0;

  /**
    Clears the inactivity timeout. No inactivity timeouts will be
    sent until set_inactivity_timeout() is used to reset the
    inactivity timeout.
    清除 inactivity timeout。在调用 set_inactivity_timeout() 重新设置之前，
    不会再有 inactivity timeout 事件发送。
  */
  virtual void cancel_inactivity_timeout() = 0;

  /** Set the action to use a continuation.
      The action continuation will be called with an event if there is no pending I/O operation
      to receive the event.

      Pass @c nullptr to disable.

      @internal Subclasses should implement this if they support actions. This abstract class does
      not. If the subclass doesn't have an action this method is silently ignored.
  */
  virtual void
  set_action(Continuation *)
  {
    return;
  }

  // 以下四个方法是对 NetHandler 中对应方法的封装，其实是通过成员 nh 调用了 NetHandler 的方法
  // 添加 NetVConnection 到 keep alive 队列
  virtual void add_to_keep_alive_queue() = 0;

  // 从 keep alive 队列移除 NetVConnection
  virtual void remove_from_keep_alive_queue() = 0;

  // 添加 NetVConnection 到 active 队列
  virtual bool add_to_active_queue() = 0;

  // 返回当前 active timeout 的值（纳秒）
  /** @return the current active_timeout value in nanosecs */
  virtual ink_hrtime get_active_timeout() = 0;

  // 返回当前 inactivity timeout 的值（纳秒）
  /** @return current inactivity_timeout value in nanosecs */
  virtual ink_hrtime get_inactivity_timeout() = 0;

  // 特殊机制：如果写操作导致缓存区为空（MIOBuffer 内没有数据可写了），就使用指定的事件回调状态机
  /** Force an @a event if a write operation empties the write buffer.

      此机制简称为：WBE
      在 IOCoreNet 处理中，如果我们设置了这个特殊的机制，那么：
        在下一次从 MIOBuffer 向 socket fd 写数据的过程中，
        如果 MIOBuffer 中当前可用于写操作的数据都被写完：
          将使用指定的 event 回调状态机

      This event will be sent to the VIO, the same place as other IO events.
      Use an @a event value of 0 to cancel the trap.
      如果传入的 event 为 0，表示取消这个操作。
      同其它 IO 事件一样，回调时传递的是 VIO 数据类型.

      这个操作是单次触发，如果需要重复触发，就需要再次设置。

      PS：在 NetHandler 回调的 write_to_net_io() 方法中的回调逻辑如下：
          如果 MIOBuffer 有剩余空间，则首先发送 WRITE READY 回调状态机填充 MIOBuffer
          然后开始将 MIOBuffer 里的数据写入 socket fd
          保存 WBE 到 wbe_event，wbe_event = WBE；
          如果 MIOBuffer 被写空
              清除 WBE 状态，发送 = 0
          如果完成了 VIO，发送 WRITE COMPLETE 回调状态机，结束写操作
          如果已经发送 WRITE READY 并且设置了 WBE
              发送 wbe_event 回调状态机，如果回调返回值不是 EVENT_CONT，结束写操作
          如果之前没有发送 WRITE READY 回调过状态机
              发送 WRITE READY 回调状态机，如果返回值不是 EVENT_CONT，结束写操作
          如果 MIOBuffer 被写空，禁止 VC 上的后续事件，结束写操作
          重新调度读、写操作

      The event is sent only the next time the write buffer is emptied, not
      every future time. The event is sent only if otherwise no event would
      be generated.
   */
  virtual void trapWriteBufferEmpty(int event = VC_EVENT_WRITE_READY);

  // 返回 local sockaddr 对象的指针，支持 IPv4 和 IPv6
  /** Returns local sockaddr storage. */
  sockaddr const *get_local_addr();

  /** Returns local ip.
      @deprecated get_local_addr() should be used instead for AF_INET6 compatibility.
  */
  // 返回 local ip，只支持 IPv4，不建议使用的老旧方法，即将废弃
  in_addr_t get_local_ip();

  // 返回 local port
  /** Returns local port. */
  uint16_t get_local_port();

  // 返回 remote sockaddr 对象的指针，支持 IPv4 和 IPv6
  /** Returns remote sockaddr storage. */
  sockaddr const *get_remote_addr();
  IpEndpoint const &get_remote_endpoint();

  // 返回 remote ip
  /** Returns remote ip.
      @deprecated get_remote_addr() should be used instead for AF_INET6 compatibility.
  */
  in_addr_t get_remote_ip();

  // 返回 remote port
  /** Returns remote port. */
  uint16_t get_remote_port();

  /** Set the context of NetVConnection.
   * The context is ONLY set once and will not be changed.
   *
   * @param context The context to be set.
   */
  void
  set_context(NetVConnectionContext_t context)
  {
    ink_assert(NET_VCONNECTION_UNSET == netvc_context);
    netvc_context = context;
  }

  /** Get the context.
   * @return the context of current NetVConnection
   */
  NetVConnectionContext_t
  get_context() const
  {
    return netvc_context;
  }

  /*
   * 保存了用户设置的结构体
   * 在 ATS 中将对主体对象的设置信息独立出来的做法，在多个地方都有应用，
   * 这样做可以在创建一组相同设置的对象时共用同一个设置实例，以减少对内存的占用
   */
  /** Structure holding user options. */
  NetVCOptions options;

  /** Attempt to push any changed options down */
  virtual void apply_options() = 0;

  //
  // Private
  // 以下方法不建议直接调用，没有声明为 private 类型是为了顺利通过代码的编译
  //

  // The following variable used to obtain host addr when transparency
  // is enabled by SocksProxy
  // 当 SocksProxy 的 transparency 打开时，用来获取主机地址
  SocksAddrType socks_addr;

  // 指定 NetVConnection 的属性
  // 用得比较多的就是 HTTPProxyPort::TRANSPORT_BLIND_TUNNEL
  unsigned int attributes;
  // 指向持有此 NetVConnection 的 EThread
  EThread *thread;

  /// PRIVATE: The public interface is VIO::reenable()
  void reenable(VIO *vio) override = 0;

  /// PRIVATE: The public interface is VIO::reenable()
  void reenable_re(VIO *vio) override = 0;

  /// PRIVATE
  ~NetVConnection() override {}
  /**
    PRIVATE: instances of NetVConnection cannot be created directly
    by the state machines. The objects are created by NetProcessor
    calls like accept connect_re() etc. The constructor is public
    just to avoid compile errors.

  */
  NetVConnection();

  virtual SOCKET get_socket() = 0;

  /** Set the TCP initial congestion window */
  virtual int set_tcp_init_cwnd(int init_cwnd) = 0;

  /** Set the TCP congestion control algorithm */
  virtual int set_tcp_congestion_control(int side) = 0;

  /** Set local sock addr struct. */
  virtual void set_local_addr() = 0;

  /** Set remote sock addr struct. */
  virtual void set_remote_addr() = 0;

  // for InkAPI
  bool
  get_is_internal_request() const
  {
    return is_internal_request;
  }

  void
  set_is_internal_request(bool val = false)
  {
    is_internal_request = val;
  }

  /// Get the transparency state.
  bool
  get_is_transparent() const
  {
    return is_transparent;
  }
  /// Set the transparency state.
  void
  set_is_transparent(bool state = true)
  {
    is_transparent = state;
  }

  virtual int
  populate_protocol(std::string_view *results, int n) const
  {
    return 0;
  }

  virtual const char *
  protocol_contains(std::string_view prefix) const
  {
    return nullptr;
  }

  // noncopyable
  NetVConnection(const NetVConnection &) = delete;
  NetVConnection &operator=(const NetVConnection &) = delete;

protected:
  IpEndpoint local_addr;
  IpEndpoint remote_addr;

  bool got_local_addr;
  bool got_remote_addr;

  bool is_internal_request;
  /// Set if this connection is transparent.
  bool is_transparent;
  /// Set if the next write IO that empties the write buffer should generate an event.
  int write_buffer_empty_event;
  /// NetVConnection Context.
  NetVConnectionContext_t netvc_context;
};

inline NetVConnection::NetVConnection()
  : AnnotatedVConnection(nullptr),
    attributes(0),
    thread(nullptr),
    got_local_addr(false),
    got_remote_addr(false),
    is_internal_request(false),
    is_transparent(false),
    write_buffer_empty_event(0),
    netvc_context(NET_VCONNECTION_UNSET)
{
  ink_zero(local_addr);
  ink_zero(remote_addr);
}

inline void
NetVConnection::trapWriteBufferEmpty(int event)
{
  write_buffer_empty_event = event;
}
