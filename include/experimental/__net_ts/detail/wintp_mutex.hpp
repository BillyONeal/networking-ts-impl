//
// detail/wintp_mutex.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_WINTP_MUTEX_HPP
#define NET_TS_DETAIL_WINTP_MUTEX_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>

#if defined(NET_TS_WINDOWS)

#include <experimental/__net_ts/detail/socket_types.hpp>
#include <experimental/__net_ts/detail/noncopyable.hpp>
#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

struct wintp_mutex {
  void lock() noexcept { AcquireSRWLockExclusive(&srw); }
  void unlock() noexcept { ReleaseSRWLockExclusive(&srw); }
  wintp_mutex() = default;
  SRWLOCK srw = SRWLOCK_INIT;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // defined(NET_TS_WINDOWS)

#endif // NET_TS_DETAIL_WIN_MUTEX_HPP
