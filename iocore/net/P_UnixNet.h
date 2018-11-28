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

#include <bitset>

#include "tscore/ink_platform.h"

#define USE_EDGE_TRIGGER_EPOLL 1
#define USE_EDGE_TRIGGER_KQUEUE 1
#define USE_EDGE_TRIGGER_PORT 1

#define EVENTIO_NETACCEPT 1
#define EVENTIO_READWRITE_VC 2
#define EVENTIO_DNS_CONNECTION 3
#define EVENTIO_UDP_CONNECTION 4
#define EVENTIO_ASYNC_SIGNAL 5

#if TS_USE_EPOLL
#ifdef USE_EDGE_TRIGGER_EPOLL
#define USE_EDGE_TRIGGER 1
#define EVENTIO_READ (EPOLLIN | EPOLLET)
#define EVENTIO_WRITE (EPOLLOUT | EPOLLET)
#else
#define EVENTIO_READ EPOLLIN
#define EVENTIO_WRITE EPOLLOUT
#endif
#define EVENTIO_ERROR (EPOLLERR | EPOLLPRI | EPOLLHUP)
#endif

#if TS_USE_KQUEUE
#ifdef USE_EDGE_TRIGGER_KQUEUE
#define USE_EDGE_TRIGGER 1
#define INK_EV_EDGE_TRIGGER EV_CLEAR
#else
#define INK_EV_EDGE_TRIGGER 0
#endif
#define EVENTIO_READ INK_EVP_IN
#define EVENTIO_WRITE INK_EVP_OUT
#define EVENTIO_ERROR (0x010 | 0x002 | 0x020) // ERR PRI HUP
#endif
#if TS_USE_PORT
#ifdef USE_EDGE_TRIGGER_PORT
#define USE_EDGE_TRIGGER 1
#endif
#define EVENTIO_READ POLLIN
#define EVENTIO_WRITE POLLOUT
#define EVENTIO_ERROR (POLLERR | POLLPRI | POLLHUP)
#endif

struct PollDescriptor;
typedef PollDescriptor *EventLoop;

class UnixNetVConnection;
class UnixUDPConnection;
struct DNSConnection;
struct NetAccept;
// 向 poll fd 添加文件句柄，以及修改 fd 事件状态
struct EventIO {
  int fd = -1;
#if TS_USE_KQUEUE || TS_USE_EPOLL && !defined(USE_EDGE_TRIGGER) || TS_USE_PORT
  int events = 0;
#endif
  EventLoop event_loop = nullptr;
  int type             = 0;
  union {
    Continuation *c;
    UnixNetVConnection *vc;
    DNSConnection *dnscon;
    NetAccept *na;
    UnixUDPConnection *uc;
  } data;
  // 在需要将一个文件描述符以及文件描述符的延伸（NetVC, UDPVC, DNSConn, NetAccpet）添加到
  // PollDescriptor 时，可以通过 EventIO 提供的 start 方法
  int start(EventLoop l, DNSConnection *vc, int events);
    // type = EVENTIO_DNS_CONNECTION
  int start(EventLoop l, NetAccept *vc, int events);
    // type = EVENTIO_NETACCEPT
  int start(EventLoop l, UnixNetVConnection *vc, int events);
    // type = EVENTIO_READWRITE_VC
  int start(EventLoop l, UnixUDPConnection *vc, int events);
    // type = EVENTIO_UDP_CONNECTION
  /*
   * 上述四个方法，都会设置成员 type 的值，最终都是调用如下的基础方法.
   * 这个基础方法是支持多种 IO Poll 平台，如：epoll，kqueue，port
   * 这个方法通常不应该被直接调用，因为这个方法没有对 type 进行设置
   * 
   * 参数说明：
   *   - EventLoop 实际是 poll fd 的封装 PollDescriptor
   *   - Continuation 则用于回调
   *   - events 表示所关注的事件，通常应该使用：EVENTIO_READ, EVENTIO_WRITE, EVENTIO_ERROR 
   *     三者之一或者它们的'或'值
   */
  int start(EventLoop l, int fd, Continuation *c, int events);
  // Change the existing events by adding modify(EVENTIO_READ)
  // or removing modify(-EVENTIO_READ), for level triggered I/O
  int modify(int events);
  // Refresh the existing events (i.e. KQUEUE EV_CLEAR), for edge triggered I/O
  int refresh(int events);
  int stop();
  int close();
  EventIO() { data.c = nullptr; }
};

#include "P_Net.h"
#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_NetAccept.h"
#include "P_DNSConnection.h"
#include "P_UnixUDPConnection.h"
#include "P_UnixPollDescriptor.h"
#include <limits>

class UnixNetVConnection;
class NetHandler;
typedef int (NetHandler::*NetContHandler)(int, void *);
typedef unsigned int uint32;

extern ink_hrtime last_throttle_warning;
extern ink_hrtime last_shedding_warning;
extern ink_hrtime emergency_throttle_time;
extern int net_connections_throttle;
extern bool net_memory_throttle;
extern int fds_throttle;
extern int fds_limit;
extern ink_hrtime last_transient_accept_error;
extern int http_accept_port_number;

//
// Configuration Parameter had to move here to share
// between UnixNet and UnixUDPNet or SSLNet modules.
// Design notes are in Memo.NetDesign
//

#define THROTTLE_FD_HEADROOM (128 + 64) // CACHE_DB_FDS + 64

#define TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY HRTIME_HOURS(24)

// also the 'throttle connect headroom'
#define EMERGENCY_THROTTLE 16
#define THROTTLE_AT_ONCE 5
#define HYPER_EMERGENCY_THROTTLE 6

#define NET_THROTTLE_ACCEPT_HEADROOM 1.1  // 10%
#define NET_THROTTLE_CONNECT_HEADROOM 1.0 // 0%
#define NET_THROTTLE_MESSAGE_EVERY HRTIME_MINUTES(10)

#define PRINT_IP(x) ((uint8_t *)&(x))[0], ((uint8_t *)&(x))[1], ((uint8_t *)&(x))[2], ((uint8_t *)&(x))[3]

// function prototype needed for SSLUnixNetVConnection
unsigned int net_next_connection_number();

struct PollCont : public Continuation {
  /*
   * 指向此 PollCont 的上层状态机 NetHandler
   * 可以理解为 Event 中的 Continuation 成员
   *     由于一个 PollCont 中的所有 EventIO 的上层状态机都是同一个 NetHandler
   *     因此，把这个 NetHandler 的对象直接放在了 PollCont 中 
   */
  NetHandler *net_handler;
  // Poll 描述符封装，主要描述符，用来保存 epoll fd 等
  PollDescriptor *pollDescriptor;
  // Poll 描述符封装，但是这个主要用于 UDP 在 EAGAIN 错误时的子状态机 UDPReadContinuation
  // 在这个状态机中只使用了 pfd[] 和 nfds
  PollDescriptor *nextPollDescriptor;
  /*
   * 由构造函数初始化，在 pollEvent() 方法内使用
   * 但是如果 net_handler 不为空，则会在每次调用 pollEvent() 时，
   *     按一定规则重置为 0 或者 net_config_poll_timeout
   */
  int poll_timeout;

  /*
   * 构造函数
   * 该构造函数用于 UDPPollCont
   * 此时成员 net_handler == NULL，那么 pollEvent() 就不会重置 poll_timeout
   * 在 ATS 所有代码中，pt 参数没有显式传入过，因此总是使用默认值
   */
  PollCont(Ptr<ProxyMutex> &m, int pt = net_config_poll_timeout);
  /*
   * 该构造函数用于 PollCont
   * 此时成员 net_handler 被初始化为 nh
   *     但是在 PollCont 的使用中，并未引用过 PollCont 内的 net_handler 成员
   *     从 pollEvent() 的设计来看，原本应该是在 NetHandler::mainNetEvent 中调用 pollEvent()
   *     但是抽象设计好像不太成功，在 NetHandler::mainNetEvent 重新实现了一遍 pollEvent() 的逻辑
   * 在 ATS 所有代码中，pt 参数没有显式传入过，因此总是使用默认值
   */
  PollCont(Ptr<ProxyMutex> &m, NetHandler *nh, int pt = net_config_poll_timeout);
  ~PollCont() override;
  // 事件处理函数，实际是对 do_poll 的封装
  int pollEvent(int, Event *);
  void do_poll(ink_hrtime timeout);
};

/**
  NetHandler is the processor of NetVC for the Net sub-system. The NetHandler
  is the core component of the Net sub-system. Once started, it is responsible
  for polling socket fds and perform the I/O tasks in NetVC.

  NetHandler 是 Net 子系统的 NetVC 处理器。NetHandler 是 Net 子系统的核心组件。
  一旦启动，它负责轮询套接字 fds 并在 NetVC 中执行 I/O 任务。

  The NetHandler is executed periodically to perform read/write tasks for
  NetVConnection. The NetHandler::mainNetEvent() should be viewed as a part of
  EThread::execute() loop. This is the reason that Net System is a sub-system.

  定期执行 NetHandler 来执行 NetVConnection 的读写任务。应将 NetHandler::mainNetEvent
  视为 EThread::execute() 循环的一部分。这就是 Net 系统是一个子系统的原因。

  By get_NetHandler(this_ethread()), you can get the NetHandler object that
  runs inside the current EThread and then @c startIO / @c stopIO which
  assign/release a NetVC to/from NetHandler. Before you call these functions,
  holding the mutex of this NetHandler is required.

  通过 get_NetHandler(this_ethread())，你可以获得当前 EThread 内运行的 NetHandler 对象，
  然后获取 @c startIO / @c stopIO，它将 NetVC 分配给 NetHandler，或从 NetHandler 中释放
  NetVC。在调用这些函数前，必须持有该 NetHandler 的 mutex。

  The NetVConnection provides a set of do_io functions through which you can
  specify continuations to be called back by its NetHandler. These function
  calls do not block. Instead they return an VIO object and schedule the
  callback to the continuation passed in when there are I/O events occurred.

  NetVConnection 提供一组的 do_io 函数，通过它们可以指定由 NetHandler 回调的
  continuations。这些函数调用不会阻塞。相反，它们返回一个 VIO 对象，并在发生 I/O 
  事件时将调度的回调传入到 continuation。

  Multi-thread scheduler:
  多线程调度器：

  The NetHandler should be viewed as multi-threaded schedulers which process
  NetVCs from their queues. The NetVC can be made of NetProcessor (allocate_vc)
  either by directly adding a NetVC to the queue (NetHandler::startIO), or more
  conveniently, calling a method service call (NetProcessor::connect_re) which
  synthesizes the NetVC and places it in the queue.

  应将 NetHandler 视为多线程调度器，从队列中处理 NetVC。NetVC 可以由 NetProcessor (allocate_vc)
  组成，可以直接 NetVC 添加到队列 (NetHandler::startIO)，或者更方便地，调用一个
  方法服务调用 (NetProcessor::connect_re) 来合成 NetVC 并将其放置到队列。

  Callback event codes:
  回调事件码:

  These event codes for do_io_read and reenable(read VIO) task:
    VC_EVENT_READ_READY, VC_EVENT_READ_COMPLETE,
    VC_EVENT_EOS, VC_EVENT_ERROR

  These event codes for do_io_write and reenable(write VIO) task:
    VC_EVENT_WRITE_READY, VC_EVENT_WRITE_COMPLETE
    VC_EVENT_ERROR

  There is no event and callback for do_io_shutdown / do_io_close task.
  do_io_shutdown / do_io_close 任务没有事件和回调。

  NetVConnection allocation policy:
  NetVConnection 分配策略:

  NetVCs are allocated by the NetProcessor and deallocated by NetHandler.
  A state machine may access the returned, non-recurring NetVC / VIO until
  it is closed by do_io_close. For recurring NetVC, the NetVC may be
  accessed until it is closed. Once the NetVC is closed, it's the
  NetHandler's responsibility to deallocate it.
  NetVC 由 NetProcessor 分配并由 NetHandler 释放。状态机可以访问返回的非重复
  NetVC / VIO，直到 do_io_close 关闭为止。对于重复出现的 NetVC，可以访问 NetVC 直到
  它 close。一旦 NetVC 被 close 了，NetHandler 就有责任 deallocate 它。

  Before assign to NetHandler or after release from NetHandler, it's the
  NetVC's responsibility to deallocate itself.
  在分配给 NetHandler 之前或从 NetHandler 发布之后，NetVC 有责任 deallocate 自己。

 */

//
// NetHandler
//
// A NetHandler handles the Network IO operations.  It maintains
// lists of operations at multiples of it's periodicity.
//
class NetHandler : public Continuation, public EThread::LoopTailHandler
{
  using self_type = NetHandler; ///< Self reference type.
public:
  // @a thread and @a trigger_event are redundant - you can get the former from the latter.
  // If we don't get rid of @a trigger_event we should remove @a thread.
  EThread *thread      = nullptr;
  Event *trigger_event = nullptr;
  /*
   * 如下两个队列:
   * 在 NetHandler::mainNetEvent 执行时，会首先将 (read|write)_enable_list 全部取出，导入到 (read|write)_ready_list
   * 另外在执行同步 reenable 时，也会将 vc 直接放入此队列
   * 在 EThread A 中要 reenable 一个 VC，这个 VC 也是由 EThread A 管理的，此时就属于同步 reenable
   */
  QueM(UnixNetVConnection, NetState, read, ready_link) read_ready_list;
  QueM(UnixNetVConnection, NetState, write, ready_link) write_ready_list;
  /* 
   * 保存当前 EThread 管理的所有 VC 
   */
  Que(UnixNetVConnection, link) open_list;
  /*
   * 在 InactivityCop 状态机中遍历 open_list，把 vc->thread == this_ethread() 的 vc 放入这个 cop_list
   * 然后对 cop_list 内的 vc 逐个检查超时情况
   * PS: 在 open_list 内的 vc，不存在 vc->thread 不是 this_ethread() 的情况
   */
  DList(UnixNetVConnection, cop_link) cop_list;
  /*
   * 如下两个队列:
   * 在执行异步 reenable 时，将 vc 直接放入原子队列
   * 在 EThread A 中要 reenable 一个 VC，但是这个 VC 是由 EThread B 管理的，此时就属于异步 reenable
   */
  ASLLM(UnixNetVConnection, NetState, read, enable_link) read_enable_list;
  ASLLM(UnixNetVConnection, NetState, write, enable_link) write_enable_list;
  /* 
   * 由上层状态机，如 HttpSM 等，通过 add_to_keep_alive_queue 将 vc 放入，用于实现 keep alive timeout
   * 在 InactivityCop 中，通过 manage_keep_alive_queue 来清理
   * 另外还提供了 remove_from_keep_alive_queue 的方法，从队列中删除 vc
   */
  Que(UnixNetVConnection, keep_alive_queue_link) keep_alive_queue;
  uint32_t keep_alive_queue_size = 0;
  /*
   * 由上层状态机，如 HttpSM 等，通过 add_to_active_queue 将 vc 放入，用于实现 inactive timeout
   * 在 InactivityCop 中，通过 manage_active_queue 来清理
   * 另外还提供了 remove_from_active_queue 的方法，从队列中删除 vc
   * 注意：active_queue 和 keep_alive_queue 是互斥的，一个 NetVC 不可以同时出现在这两个队列中，
   * 这由 add_(keep_alive|active)_queue 保证.
   */
  Que(UnixNetVConnection, active_queue_link) active_queue;
  uint32_t active_queue_size = 0;

  /* 如上 8 个队列，其中 (read|write)_enable_list 两个队列是原子队列，可以不加锁直接操作，
   * 其它六个队列   都需要加锁才能操作 */

  /// configuration settings for managing the active and keep-alive queues
  struct Config {
    uint32_t max_connections_in                 = 0;
    uint32_t max_connections_active_in          = 0;
    uint32_t inactive_threshold_in              = 0;
    uint32_t transaction_no_activity_timeout_in = 0;
    uint32_t keep_alive_no_activity_timeout_in  = 0;
    uint32_t default_inactivity_timeout         = 0;

    /** Return the address of the first value in this struct.

        Doing updates is much easier if we treat this config struct as an array.
        Making it a method means the knowledge of which member is the first one
        is localized to this struct, not scattered about.
     */
    uint32_t &operator[](int n) { return *(&max_connections_in + n); }
  };
  /** Static global config, set and updated per process.

      This is updated asynchronously and then events are sent to the NetHandler instances per thread
      to copy to the per thread config at a convenient time. Because these are updated independently
      from the command line, the update events just copy a single value from the global to the
      local. This mechanism relies on members being identical types.
  */
  static Config global_config;
  Config config; ///< Per thread copy of the @c global_config
  // Active and keep alive queue values that depend on other configuration values.
  // These are never updated directly, they are computed from other config values.
  uint32_t max_connections_per_thread_in        = 0;
  uint32_t max_connections_active_per_thread_in = 0;
  /// Number of configuration items in @c Config.
  static constexpr int CONFIG_ITEM_COUNT = sizeof(Config) / sizeof(uint32_t);
  /// Which members of @c Config the per thread values depend on.
  /// If one of these is updated, the per thread values must also be updated.
  static const std::bitset<CONFIG_ITEM_COUNT> config_value_affects_per_thread_value;
  /// Set of thread types in which nethandlers are active.
  /// This enables signaling the correct instances when the configuration is updated.
  /// Event type threads that use @c NetHandler must set the corresponding bit.
  static std::bitset<std::numeric_limits<unsigned int>::digits> active_thread_types;

  int mainNetEvent(int event, Event *data);
  int waitForActivity(ink_hrtime timeout) override;
  void process_enabled_list();
  void process_ready_list();
  void manage_keep_alive_queue();
  bool manage_active_queue(bool ignore_queue_size);
  void add_to_keep_alive_queue(UnixNetVConnection *vc);
  void remove_from_keep_alive_queue(UnixNetVConnection *vc);
  bool add_to_active_queue(UnixNetVConnection *vc);
  void remove_from_active_queue(UnixNetVConnection *vc);

  /// Per process initialization logic.
  static void init_for_process();
  /// Update configuration values that are per thread and depend on other configuration values.
  void configure_per_thread_values();

  /**
    Start to handle read & write event on a UnixNetVConnection.
    Initial the socket fd of netvc for polling system.
    Only be called when holding the mutex of this NetHandler.

    @param netvc UnixNetVConnection to be managed by this NetHandler.
    @return 0 on success, netvc->nh set to this NetHandler.
            -ERRNO on failure.
   */
  int startIO(UnixNetVConnection *netvc);
  /**
    Stop to handle read & write event on a UnixNetVConnection.
    Remove the socket fd of netvc from polling system.
    Only be called when holding the mutex of this NetHandler and must call stopCop(netvc) first.

    @param netvc UnixNetVConnection to be released.
    @return netvc->nh set to nullptr.
   */
  void stopIO(UnixNetVConnection *netvc);

  /**
    Start to handle active timeout and inactivity timeout on a UnixNetVConnection.
    Put the netvc into open_list. All NetVCs in the open_list is checked for timeout by InactivityCop.
    Only be called when holding the mutex of this NetHandler and must call startIO(netvc) first.

    @param netvc UnixNetVConnection to be managed by InactivityCop
   */
  void startCop(UnixNetVConnection *netvc);
  /**
    Stop to handle active timeout and inactivity on a UnixNetVConnection.
    Remove the netvc from open_list and cop_list.
    Also remove the netvc from keep_alive_queue and active_queue if its context is IN.
    Only be called when holding the mutex of this NetHandler.

    @param netvc UnixNetVConnection to be released.
   */
  void stopCop(UnixNetVConnection *netvc);

  // Signal the epoll_wait to terminate.
  void signalActivity() override;

  /**
    Release a netvc and free it.

    @param netvc UnixNetVConnection to be deattached.
   */
  void free_netvc(UnixNetVConnection *netvc);

  NetHandler();

private:
  void _close_vc(UnixNetVConnection *vc, ink_hrtime now, int &handle_event, int &closed, int &total_idle_time,
                 int &total_idle_count);

  /// Static method used as the callbackf for runtime configuration updates.
  static int update_nethandler_config(const char *name, RecDataT, RecData data, void *);
};

// 封装了专门获取 NetHandler 的操作
static inline NetHandler *
get_NetHandler(EThread *t)
{
  // 使用宏定义构造指针
  return (NetHandler *)ETHREAD_GET_PTR(t, unix_netProcessor.netHandler_offset);
}
static inline PollCont *
get_PollCont(EThread *t)
{
  return (PollCont *)ETHREAD_GET_PTR(t, unix_netProcessor.pollCont_offset);
}
static inline PollDescriptor *
get_PollDescriptor(EThread *t)
{
  PollCont *p = get_PollCont(t);
  return p->pollDescriptor;
}

enum ThrottleType {
  ACCEPT,
  CONNECT,
};

TS_INLINE int
net_connections_to_throttle(ThrottleType t)
{
  double headroom = t == ACCEPT ? NET_THROTTLE_ACCEPT_HEADROOM : NET_THROTTLE_CONNECT_HEADROOM;
  int64_t sval    = 0;

  NET_READ_GLOBAL_DYN_SUM(net_connections_currently_open_stat, sval);
  int currently_open = (int)sval;
  // deal with race if we got to multiple net threads
  if (currently_open < 0)
    currently_open = 0;
  return (int)(currently_open * headroom);
}

TS_INLINE void
check_shedding_warning()
{
  ink_hrtime t = Thread::get_hrtime();
  if (t - last_shedding_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_shedding_warning = t;
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "number of connections reaching shedding limit");
  }
}

TS_INLINE bool
check_net_throttle(ThrottleType t)
{
  int connections = net_connections_to_throttle(t);

  if (net_connections_throttle != 0 && connections >= net_connections_throttle)
    return true;

  return false;
}

TS_INLINE void
check_throttle_warning(ThrottleType type)
{
  ink_hrtime t = Thread::get_hrtime();
  if (t - last_throttle_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_throttle_warning = t;
    int connections       = net_connections_to_throttle(type);
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR,
                     "too many connections, throttling.  connection_type=%s, current_connections=%d, net_connections_throttle=%d",
                     type == ACCEPT ? "ACCEPT" : "CONNECT", connections, net_connections_throttle);
  }
}

TS_INLINE int
change_net_connections_throttle(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void)token;
  (void)data_type;
  (void)value;
  (void)data;
  int throttle = fds_limit - THROTTLE_FD_HEADROOM;
  if (fds_throttle == 0) {
    net_connections_throttle = fds_throttle;
  } else if (fds_throttle < 0) {
    net_connections_throttle = throttle;
  } else {
    net_connections_throttle = fds_throttle;
    if (net_connections_throttle > throttle)
      net_connections_throttle = throttle;
  }
  return 0;
}

// 1  - transient
// 0  - report as warning
// -1 - fatal
TS_INLINE int
accept_error_seriousness(int res)
{
  switch (res) {
  case -EAGAIN:
  case -ECONNABORTED:
  case -ECONNRESET: // for Linux
  case -EPIPE:      // also for Linux
    return 1;
  case -EMFILE:
  case -ENOMEM:
#if defined(ENOSR) && !defined(freebsd)
  case -ENOSR:
#endif
    ink_assert(!"throttling misconfigured: set too high");
#ifdef ENOBUFS
  // fallthrough
  case -ENOBUFS:
#endif
#ifdef ENFILE
  case -ENFILE:
#endif
    return 0;
  case -EINTR:
    ink_assert(!"should be handled at a lower level");
    return 0;
#if defined(EPROTO) && !defined(freebsd)
  case -EPROTO:
#endif
  case -EOPNOTSUPP:
  case -ENOTSOCK:
  case -ENODEV:
  case -EBADF:
  default:
    return -1;
  }
}

TS_INLINE void
check_transient_accept_error(int res)
{
  ink_hrtime t = Thread::get_hrtime();
  if (!last_transient_accept_error || t - last_transient_accept_error > TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY) {
    last_transient_accept_error = t;
    Warning("accept thread received transient error: errno = %d", -res);
#if defined(linux)
    if (res == -ENOBUFS || res == -ENFILE)
      Warning("errno : %d consider a memory upgrade", -res);
#endif
  }
}

//
// Disable a UnixNetVConnection
//
static inline void
read_disable(NetHandler *nh, UnixNetVConnection *vc)
{
  if (!vc->write.enabled) {
    vc->set_inactivity_timeout(0);
    Debug("socket", "read_disable updating inactivity_at %" PRId64 ", NetVC=%p", vc->next_inactivity_timeout_at, vc);
  }
  vc->read.enabled = 0;
  nh->read_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_READ);
}

static inline void
write_disable(NetHandler *nh, UnixNetVConnection *vc)
{
  if (!vc->read.enabled) {
    vc->set_inactivity_timeout(0);
    Debug("socket", "write_disable updating inactivity_at %" PRId64 ", NetVC=%p", vc->next_inactivity_timeout_at, vc);
  }
  vc->write.enabled = 0;
  nh->write_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_WRITE);
}

TS_INLINE int
EventIO::start(EventLoop l, DNSConnection *vc, int events)
{
  type = EVENTIO_DNS_CONNECTION;
  return start(l, vc->fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, NetAccept *vc, int events)
{
  type = EVENTIO_NETACCEPT;
  return start(l, vc->server.fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, UnixNetVConnection *vc, int events)
{
  type = EVENTIO_READWRITE_VC;
  return start(l, vc->con.fd, (Continuation *)vc, events);
}
TS_INLINE int
EventIO::start(EventLoop l, UnixUDPConnection *vc, int events)
{
  type = EVENTIO_UDP_CONNECTION;
  return start(l, vc->fd, (Continuation *)vc, events);
}

// 该方法用于关闭 fd，首先调用 stop 方法停止 polling，然后根据 type 的类型，
// 调用该类型的 close 方法完成 fd 的关闭
// 但是 close 只负责关闭 DNS, NetAccpet, VC 三种类型，对于 UDP，则不可以调用 close 方法，这是为什么？
TS_INLINE int
EventIO::close()
{
  stop();
  switch (type) {
  default:
    ink_assert(!"case");
  // fallthrough
  case EVENTIO_DNS_CONNECTION:
    return data.dnscon->close();
    break;
  case EVENTIO_NETACCEPT:
    return data.na->server.close();
    break;
  case EVENTIO_READWRITE_VC:
    return data.vc->con.close();
    break;
  }
  return -1;
}

TS_INLINE int
EventIO::start(EventLoop l, int afd, Continuation *c, int e)
{
  data.c     = c;
  fd         = afd;
  event_loop = l;
#if TS_USE_EPOLL
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events   = e;
  ev.data.ptr = this;
#ifndef USE_EDGE_TRIGGER
  events = e;
#endif
  // 最后调用 epoll_ctl 把 ev 添加到 epoll fd 的事件池中，然后可以通过 epoll_wait 获取结果
  // 在 ev 中包含了指向该 EventIO 实例的指针，在每一个 UnixNetVConnection 里都有一个 EventIO 类型的成员
  return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
#endif
#if TS_USE_KQUEUE
  events = e;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  return kevent(l->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
#endif
#if TS_USE_PORT
  events     = e;
  int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
  Debug("iocore_eventio", "[EventIO::start] e(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, events, retval,
        retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
  return retval;
#endif
}

TS_INLINE int
EventIO::modify(int e)
{
  ink_assert(event_loop);
#if TS_USE_EPOLL && !defined(USE_EDGE_TRIGGER)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  int new_events = events, old_events = events;
  if (e < 0)
    new_events &= ~(-e);
  else
    new_events |= e;
  events      = new_events;
  ev.events   = new_events;
  ev.data.ptr = this;
  if (!new_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
  else if (!old_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  else
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
#endif
#if TS_USE_KQUEUE && !defined(USE_EDGE_TRIGGER)
  int n = 0;
  struct kevent ev[2];
  int ee = events;
  if (e < 0) {
    ee &= ~(-e);
    if ((-e) & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, this);
    if ((-e) & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, this);
  } else {
    ee |= e;
    if (e & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
    if (e & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  events = ee;
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
  else
    return 0;
#endif
#if TS_USE_PORT
  int n  = 0;
  int ne = e;
  if (e < 0) {
    if (((-e) & events)) {
      ne = ~(-e) & events;
      if ((-e) & EVENTIO_READ)
        n++;
      if ((-e) & EVENTIO_WRITE)
        n++;
    }
  } else {
    if (!(e & events)) {
      ne = events | e;
      if (e & EVENTIO_READ)
        n++;
      if (e & EVENTIO_WRITE)
        n++;
    }
  }
  if (n && ne && event_loop) {
    events     = ne;
    int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
    Debug("iocore_eventio", "[EventIO::modify] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events,
          retval, retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
    return retval;
  }
  return 0;
#else
  (void)e; // ATS_UNUSED
  return 0;
#endif
}

TS_INLINE int
EventIO::refresh(int e)
{
  ink_assert(event_loop);
#if TS_USE_KQUEUE && defined(USE_EDGE_TRIGGER)
  e = e & events;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD | INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, nullptr, 0, nullptr);
  else
    return 0;
#endif
#if TS_USE_PORT
  int n  = 0;
  int ne = e;
  if ((e & events)) {
    ne = events | e;
    if (e & EVENTIO_READ)
      n++;
    if (e & EVENTIO_WRITE)
      n++;
    if (n && ne && event_loop) {
      events     = ne;
      int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
      Debug("iocore_eventio", "[EventIO::refresh] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events,
            retval, retval < 0 ? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
      return retval;
    }
  }
  return 0;
#else
  (void)e; // ATS_UNUSED
  return 0;
#endif
}

TS_INLINE int
EventIO::stop()
{
  if (event_loop) {
    int retval = 0;
#if TS_USE_EPOLL
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    // 通过 epoll_ctl 从 epoll fd 中删除 fd
    retval    = epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
#endif
#if TS_USE_PORT
    retval = port_dissociate(event_loop->port_fd, PORT_SOURCE_FD, fd);
    Debug("iocore_eventio", "[EventIO::stop] %d[%s]=port_dissociate(%d,%d,%d)", retval, retval < 0 ? strerror(errno) : "ok",
          event_loop->port_fd, PORT_SOURCE_FD, fd);
#endif
    // 设置 event_loop 为 NULL，因为所有的操作都是通过 epoll fd 进行，这里把 event_loop 设置为 NULL，就相当于没有了 epoll fd
    event_loop = nullptr;
    return retval;
  }
  return 0;
}

TS_INLINE int
NetHandler::startIO(UnixNetVConnection *netvc)
{
  ink_assert(this->mutex->thread_holding == this_ethread());
  ink_assert(netvc->thread == this_ethread());
  int res = 0;

  PollDescriptor *pd = get_PollDescriptor(this->thread);
  if (netvc->ep.start(pd, netvc, EVENTIO_READ | EVENTIO_WRITE) < 0) {
    res = errno;
    // EEXIST should be ok, though it should have been cleared before we got back here
    if (errno != EEXIST) {
      Debug("iocore_net", "NetHandler::startIO : failed on EventIO::start, errno = [%d](%s)", errno, strerror(errno));
      return -res;
    }
  }

  if (netvc->read.triggered == 1) {
    read_ready_list.enqueue(netvc);
  }
  netvc->nh = this;
  return res;
}

TS_INLINE void
NetHandler::stopIO(UnixNetVConnection *netvc)
{
  ink_release_assert(netvc->nh == this);

  netvc->ep.stop();

  read_ready_list.remove(netvc);
  write_ready_list.remove(netvc);
  if (netvc->read.in_enabled_list) {
    read_enable_list.remove(netvc);
    netvc->read.in_enabled_list = 0;
  }
  if (netvc->write.in_enabled_list) {
    write_enable_list.remove(netvc);
    netvc->write.in_enabled_list = 0;
  }

  netvc->nh = nullptr;
}

TS_INLINE void
NetHandler::startCop(UnixNetVConnection *netvc)
{
  ink_assert(this->mutex->thread_holding == this_ethread());
  ink_release_assert(netvc->nh == this);
  ink_assert(!open_list.in(netvc));

  open_list.enqueue(netvc);
}

TS_INLINE void
NetHandler::stopCop(UnixNetVConnection *netvc)
{
  ink_release_assert(netvc->nh == this);

  open_list.remove(netvc);
  cop_list.remove(netvc);
  remove_from_keep_alive_queue(netvc);
  remove_from_active_queue(netvc);
}
