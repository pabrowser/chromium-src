// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_EPOLL_ALARM_FACTORY_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_EPOLL_ALARM_FACTORY_H_

#include "net/third_party/quic/core/quic_alarm.h"
#include "net/third_party/quic/core/quic_alarm_factory.h"
#include "net/third_party/quic/core/quic_one_block_arena.h"
#include "net/third_party/quic/platform/api/quic_epoll.h"

namespace quic {}  // namespace quic
namespace net {
class EpollServer;
}  // namespace net
namespace quic {

// Creates alarms that use the supplied net::EpollServer for timing and firing.
class QuicEpollAlarmFactory : public QuicAlarmFactory {
 public:
  explicit QuicEpollAlarmFactory(QuicEpollServer* eps);
  QuicEpollAlarmFactory(const QuicEpollAlarmFactory&) = delete;
  QuicEpollAlarmFactory& operator=(const QuicEpollAlarmFactory&) = delete;
  ~QuicEpollAlarmFactory() override;

  // QuicAlarmFactory interface.
  QuicAlarm* CreateAlarm(QuicAlarm::Delegate* delegate) override;
  QuicArenaScopedPtr<QuicAlarm> CreateAlarm(
      QuicArenaScopedPtr<QuicAlarm::Delegate> delegate,
      QuicConnectionArena* arena) override;

 private:
  QuicEpollServer* epoll_server_;  // Not owned.
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_EPOLL_ALARM_FACTORY_H_
