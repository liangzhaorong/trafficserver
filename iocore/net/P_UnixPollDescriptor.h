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

  UnixPollDescriptor.h


*****************************************************************************/
#pragma once

#include "tscore/ink_platform.h"

#if TS_USE_KQUEUE
#include <sys/event.h>
#define INK_EVP_IN 0x001
#define INK_EVP_PRI 0x002
#define INK_EVP_OUT 0x004
#define INK_EVP_ERR 0x010
#define INK_EVP_HUP 0x020
#endif

// 定义一个 PollDescriptor 可以处理的最大描述符的数量
// 这里定义为 32k = 32768 = 0x10000，如果要修改这个值，也建议采用 16 的整数倍
//   以保证在存储这些数据时实现内存边界的对齐.
#define POLL_DESCRIPTOR_SIZE 32768

typedef struct pollfd Pollfd;

/**
 * PollDescriptor 是对多种平台上的 I/O poll 操作的封装，目前 ATS 支持 epoll，kqueue，port 三种
 * 可以把 PollDescriptor 看成是一个队列：
 *   - 把 Socket FD 放入 poll fd / PollDescriptor 队列
 *   - 然后通过 Polling 操作，从 poll fd / PollDescriptor 队列中批量取出一部分 socket fd
 *   - 而且这是一个原子队列
 * 
 * EventIO 是 PollDescriptor 队列的成员，也是 PollDescriptor 对外提供的接口：
 *   - 为每一个 socket fd 提供一个 EventIO，把 Socket FD 封装到 EventIO 内
 *   - 提供把 EventIO 自身放入 PollDescriptor 队列的接口
 * 
 */

struct PollDescriptor {
  int result; // result of poll
#if TS_USE_EPOLL
  // 用于保存 epoll_create 创建的 epoll fd
  int epoll_fd;
  // nfds 和 pfd 在 TCP 的部分没有用到，目前应该只有在 UDP 里才会使用
  // 记录有多少个 fd 被添加到 epoll fd 中
  int nfds; // actual number
  // 每一个 fd 被添加到 epoll fd 前会保存到 pfd 这个数组里
  Pollfd pfd[POLL_DESCRIPTOR_SIZE];
  /*
   * 用来保存 epoll_wait 返回的 fd 状态集，result 中保存了实际的 fd 数量
   * 为什么不在 epoll_wait 处使用函数内部变量，而要固定分配一个内存区域呢？
   *     由于 epoll_wait 需要非常高频率的运行，因此这个内存区域需要非常高频次的分配和释放，
   *     即使在函数内定义为内部数组变量，那么也会频繁的从栈里分配空间，
   *     所以不如分配一个固定的内存区域
   */
  struct epoll_event ePoll_Triggered_Events[POLL_DESCRIPTOR_SIZE];
#endif
#if TS_USE_KQUEUE
  int kqueue_fd;
#endif
#if TS_USE_PORT
  int port_fd;
#endif

  // 构造函数，调用 init() 完成初始化
  PollDescriptor() { init(); }
#if TS_USE_EPOLL
  // 下面四个宏定义是一组通用方法，对于 kqueue 等其他系统的 I/O poll 操作也都是抽象为这四个方法
  // 获取 I/O poll 描述符，对于 epoll 就是 epoll fd
#define get_ev_port(a) ((a)->epoll_fd)
  // 获取指定 fd 当前的事件状态，在 epoll_wait 之后通过这个方法获取返回的 fd 的事件状态
#define get_ev_events(a, x) ((a)->ePoll_Triggered_Events[(x)].events)
  // 获取指定 fd 绑定的数据指针，对于 epoll 就是 epoll_event 结构体的 data.ptr
#define get_ev_data(a, x) ((a)->ePoll_Triggered_Events[(x)].data.ptr)
  // 准备获取下一个 fd，对于 epoll 来说这里没啥对应的操作
#define ev_next_event(a, x)
#endif

#if TS_USE_KQUEUE
  struct kevent kq_Triggered_Events[POLL_DESCRIPTOR_SIZE];
/* we define these here as numbers, because for kqueue mapping them to a combination of
 *filters / flags is hard to do. */
#define get_ev_port(a) ((a)->kqueue_fd)
#define get_ev_events(a, x) ((a)->kq_event_convert((a)->kq_Triggered_Events[(x)].filter, (a)->kq_Triggered_Events[(x)].flags))
#define get_ev_data(a, x) ((a)->kq_Triggered_Events[(x)].udata)
  int
  kq_event_convert(int16_t event, uint16_t flags)
  {
    int r = 0;

    if (event == EVFILT_READ) {
      r |= INK_EVP_IN;
    } else if (event == EVFILT_WRITE) {
      r |= INK_EVP_OUT;
    }

    if (flags & EV_EOF) {
      r |= INK_EVP_HUP;
    }
    return r;
  }
#define ev_next_event(a, x)
#endif

#if TS_USE_PORT
  port_event_t Port_Triggered_Events[POLL_DESCRIPTOR_SIZE];
#define get_ev_port(a) ((a)->port_fd)
#define get_ev_events(a, x) ((a)->Port_Triggered_Events[(x)].portev_events)
#define get_ev_data(a, x) ((a)->Port_Triggered_Events[(x)].portev_user)
#define get_ev_odata(a, x) ((a)->Port_Triggered_Events[(x)].portev_object)
#define ev_next_event(a, x)
#endif

  // 从 pfd 中分配一个地址空间
  // 仅用于 epoll 对 UDP 的支持，如使用 kqueue 等其它系统的 I/O poll 操作，则直接返回 NULL
  Pollfd *
  alloc()
  {
#if TS_USE_EPOLL
    // XXX : We need restrict max size based on definition.
    if (nfds >= POLL_DESCRIPTOR_SIZE) {
      nfds = 0;
    }
    return &pfd[nfds++];
#else
    return 0;
#endif
  }

private:
  // 初始化 epoll_fd 及事件数组
  void
  init()
  {
    result = 0;
#if TS_USE_EPOLL
    nfds     = 0;
    epoll_fd = epoll_create(POLL_DESCRIPTOR_SIZE);
    memset(ePoll_Triggered_Events, 0, sizeof(ePoll_Triggered_Events));
    memset(pfd, 0, sizeof(pfd));
#endif
#if TS_USE_KQUEUE
    kqueue_fd = kqueue();
    memset(kq_Triggered_Events, 0, sizeof(kq_Triggered_Events));
#endif
#if TS_USE_PORT
    port_fd = port_create();
    memset(Port_Triggered_Events, 0, sizeof(Port_Triggered_Events));
#endif
  }
};
