//
// detail/wintp_timer_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_WINTP_TIMER_SERVICE_HPP
#define NET_TS_DETAIL_WINTP_TIMER_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>
#include <cstddef>
#include <experimental/__net_ts/error.hpp>
#include <experimental/__net_ts/io_context.hpp>
#include <experimental/__net_ts/detail/bind_handler.hpp>
#include <experimental/__net_ts/detail/fenced_block.hpp>
#include <experimental/__net_ts/detail/memory.hpp>
#include <experimental/__net_ts/detail/noncopyable.hpp>
#include <experimental/__net_ts/detail/wait_handler.hpp>
#include <experimental/__net_ts/detail/wait_op.hpp>

#include <chrono>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

template <typename Time_Traits>
class wintp_timer_service
  : public service_base<wintp_timer_service<Time_Traits> >
{
  using hundreds_nano = ratio_multiply<ratio<100, 1>, nano>;
  using nt_ticks = chrono::duration<long long, hundreds_nano>;
  static constexpr nt_ticks epoch_adj = nt_ticks(116444736000000000ll);
public:
  // The time type.
  typedef typename Time_Traits::time_type time_type;

  // The duration type.
  typedef typename Time_Traits::duration_type duration_type;

  // The implementation type of the timer. This type is dependent on the
  // underlying implementation of the timer service.
  struct implementation_type {
    time_type expiry;
  };

  wintp_timer_service(io_context &io_context)
      : service_base<wintp_timer_service<Time_Traits>>(io_context),
        tp(dynamic_cast<tp_context&>(io_context)) {}

  ~wintp_timer_service() {
    // FIXME: Verify that there is no outstanding timers
  }

  // Destroy all user-defined handler objects owned by the service.
  void shutdown()
  {
  }

  // Construct a new timer implementation.
  void construct(implementation_type& impl)
  {
    impl.expiry = time_type();
  }

  // Destroy a timer implementation.
  void destroy(implementation_type& impl)
  {
    std::error_code ec;
    cancel(impl, ec);
  }

#if 0
  // Move-construct a new serial port implementation.
  void move_construct(implementation_type& impl,
      implementation_type& other_impl)
  {
    scheduler_.move_timer(timer_queue_, impl.timer_data, other_impl.timer_data);

    impl.expiry = other_impl.expiry;
    other_impl.expiry = time_type();

    impl.might_have_pending_waits = other_impl.might_have_pending_waits;
    other_impl.might_have_pending_waits = false;
  }

  // Move-assign from another serial port implementation.
  void move_assign(implementation_type& impl,
      deadline_timer_service& other_service,
      implementation_type& other_impl)
  {
    if (this != &other_service)
      if (impl.might_have_pending_waits)
        scheduler_.cancel_timer(timer_queue_, impl.timer_data);

    other_service.scheduler_.move_timer(other_service.timer_queue_,
        impl.timer_data, other_impl.timer_data);

    impl.expiry = other_impl.expiry;
    other_impl.expiry = time_type();

    impl.might_have_pending_waits = other_impl.might_have_pending_waits;
    other_impl.might_have_pending_waits = false;
  }
#endif

  // Cancel any asynchronous wait operations associated with the timer.
  std::size_t cancel(implementation_type& impl, std::error_code& ec)
  {
    return 0;
  }

  // Cancels one asynchronous wait operation associated with the timer.
  std::size_t cancel_one(implementation_type& impl,
      std::error_code& ec)
  {
    return 0;
  }

  // Get the expiry time for the timer as an absolute time.
  time_type expiry(const implementation_type& impl) const
  {
    return impl.expiry;
  }

  // Get the expiry time for the timer as an absolute time.
  time_type expires_at(const implementation_type& impl) const
  {
    return impl.expiry;
  }

  // Get the expiry time for the timer relative to now.
  duration_type expires_from_now(const implementation_type& impl) const
  {
    return Time_Traits::subtract(this->expiry(impl), Time_Traits::now());
  }

  // Set the expiry time for the timer as an absolute time.
  std::size_t expires_at(implementation_type& impl,
      const time_type& expiry_time, std::error_code& ec)
  {
    std::size_t count = cancel(impl, ec);
    impl.expiry = expiry_time;
    ec = std::error_code();
    return count;
  }

  // Set the expiry time for the timer relative to now.
  std::size_t expires_after(implementation_type& impl,
      const duration_type& expiry_time, std::error_code& ec)
  {
    return expires_at(impl,
        Time_Traits::add(Time_Traits::now(), expiry_time), ec);
  }

  // Set the expiry time for the timer relative to now.
  std::size_t expires_from_now(implementation_type& impl,
      const duration_type& expiry_time, std::error_code& ec)
  {
    return expires_at(impl,
        Time_Traits::add(Time_Traits::now(), expiry_time), ec);
  }

  // Start an asynchronous wait on the timer.
  template <typename Handler>
  void async_wait(implementation_type& impl, Handler& handler)
  {
  }

private:
  tp_context& tp;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_DEADLINE_TIMER_SERVICE_HPP
