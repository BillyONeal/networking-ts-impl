//
// detail/cancellable_object_owner.hpp
// ~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_OBJECT_POOL_HPP
#define NET_TS_DETAIL_OBJECT_POOL_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/noncopyable.hpp>
#include <experimental/__net_ts/detail/object_pool.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

  struct cancellable_object_base {};

  struct cancellabl_object_owner {};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_OBJECT_POOL_HPP
