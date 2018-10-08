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
#include <experimental/__net_ts/detail/wintp_scheduler.hpp>

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

  struct timer_impl {
    explicit timer_impl(wintp_timer_service &service,
                        wintp_scheduler &scheduler)
        : timer(CreateThreadpoolTimer(
              [](auto, void *ctx, auto) {
                static_cast<timer_impl *>(ctx)->invoke();
              },
              this, nullptr)),
          service(service), scheduler(scheduler) {
      // TODO: more detailed error codes
      if (!timer)
        throw std::bad_alloc();
    }

    void invoke() {
      //std::error_code ec;
      //op_queue<wait_op> q;
      //{
      //  std::lock_guard<win_mutex> grab(m);
      //  if (!cancelled_ops_.empty()) {
      //    scheduler.reserved_post(cancelled_ops_);

      //    // See if we need to schedule another wait
      //    if (!ops_.empty() && expiry != time_type{}) {
      //      auto time = expiry + epoch_adj;
      //      SetThreadpoolTimer(timer, reinterpret_cast<PFILETIME>(&time), 0, 0);
      //    }
      //    return;
      //  }
      //  swap(q, ops_);
      //  expiry = {};
      //}
      //operation *op = q.try_pop();
      //scheduler.reserved_post(q);
      //if (op) {
      //  op->complete(this, ec, /*bytes_xfered=*/0);
      //  scheduler.work_finished(); // TOOD: exception guard
      //}
    }

    //std::size_t soft_cancel()
    //{
    //  std::lock_guard<win_mutex> grab(m);
    //  if (ops_.empty())
    //    return 0;

    //  std::size_t count = 0;
    //  ops_.for_each([&](wait_op *op) {
    //    ++count;
    //    op->ec_ = error::operation_aborted;
    //  });

    //  cancelled_ops_.push(ops_);
    //  if (SetThreadpoolTimerEx(timer, nullptr, 0, 0)) {
    //    // timer will not fire. Move all of the cancelled ops to the
    //    // scheduler.
    //    scheduler.reserved_post(cancelled_ops_);
    //  }

    //  return count;
    //}

    //void initiate_cancel() {
    //  soft_cancel();
    //}

    ~timer_impl() {
      WaitForThreadpoolTimerCallbacks(timer, false);
      CloseThreadpoolTimer(timer);
    }

    PTP_TIMER timer = nullptr;
    wintp_timer_service &service;
    wintp_scheduler &scheduler;

    win_mutex m;
    time_type expiry = {};
    op_queue<wait_op> ops_;
    op_queue<wait_op> cancelled_ops_;
  };

  // The implementation type of the timer. This type is dependent on the
  // underlying implementation of the timer service.
  // FIXME: use object_pool
  using implementation_type = timer_impl*;

  wintp_timer_service(io_context &ioc)
      : service_base<wintp_timer_service<Time_Traits>>(ioc),
        tp(static_cast<tp_context&>(ioc)) {
    if (!ioc.is_thread_pool()) {
      std::abort();
    }
  }

  ~wintp_timer_service() {
    // No need to remove here, will be removed due to cancellation
    // tp.scheduler().remove_child(this);
    // FIXME: Verify that there is no outstanding timers
  }

  void initiate_cancel() {
//    if (try_set_cancel_pending("timer_service")) {
   // owner.cancel_all("timer_service");
  }

  // Destroy all user-defined handler objects owned by the service.
  void shutdown()
  {
    // should wait for all to finish.
    //cancel();
  }

  void destroy(implementation_type &impl) {
    (void)impl;
    //impl->initiate_cancel();
    //impl->drop_ref("handle");
  }

  void construct(implementation_type &t) {
    (void)t;
    //auto p = make_unique<timer_impl>(*this, tp.scheduler());
    //owner.add_child(p.get());
    //t = p.release();
  }

  // Move-construct a new serial port implementation.
  void move_construct(implementation_type &impl,
                      implementation_type &other_impl) {
    impl = move(other_impl);
  }

  // Move-assign from another serial port implementation.
  void move_assign(implementation_type& impl,
      wintp_timer_service& other_service,
      implementation_type& other_impl)
  {
    impl = move(other_impl);
  }

  // Cancel any asynchronous wait operations associated with the timer.
  std::size_t cancel(implementation_type& impl, std::error_code& ec) {
    //ec = {};
    //return impl->soft_cancel();
    return 0;
  }

  // Cancels one asynchronous wait operation associated with the timer.
  std::size_t cancel_one(implementation_type& impl,
      std::error_code& ec)
  {
    throw std::logic_error("cancel_one, not yet implemented");
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
    impl->expiry = expiry_time;
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
    // Allocate and construct an operation to wrap the handler.
    typedef wait_handler<Handler> op;
    typename op::ptr p = { std::experimental::net::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(handler);

    NET_TS_HANDLER_CREATION((scheduler_.context(),
      *p.p, "wintp_timer", &impl, 0, "async_wait"));

    //schedule_timer(impl, p.p);
    p.v = p.p = 0;
  }

private:
  //void schedule_timer(implementation_type &impl, wait_op *op) {
  //  tp.scheduler().work_started();

  //  {
  //    std::lock_guard<win_mutex> grab(impl->m);
  //    if (impl->expiry != time_type()) {
  //      bool ops_empty = impl->ops_.empty();
  //      impl->ops_.push(op);

  //      if (ops_empty && impl->cancelled_ops_.empty()) {
  //        auto time = impl->expiry + epoch_adj;
  //        SetThreadpoolTimer(impl->timer, reinterpret_cast<PFILETIME>(&time), 0,
  //          0);
  //      }
  //      return;
  //    }
  //  }
  //  tp.scheduler().reserved_post(op);
  //}
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
