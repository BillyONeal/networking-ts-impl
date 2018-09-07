//
// impl/io_context.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_IMPL_IO_CONTEXT_HPP
#define NET_TS_IMPL_IO_CONTEXT_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/completion_handler.hpp>
#include <experimental/__net_ts/detail/executor_op.hpp>
#include <experimental/__net_ts/detail/fenced_block.hpp>
#include <experimental/__net_ts/detail/handler_type_requirements.hpp>
#include <experimental/__net_ts/detail/recycling_allocator.hpp>
#include <experimental/__net_ts/detail/service_registry.hpp>
#include <experimental/__net_ts/detail/throw_error.hpp>
#include <experimental/__net_ts/detail/type_traits.hpp>
#include <experimental/tp_context>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {

template <typename Service>
inline Service& use_service(io_context& ioc)
{
  // Check that Service meets the necessary type requirements.
  (void)static_cast<execution_context::service*>(static_cast<Service*>(0));
  (void)static_cast<const execution_context::id*>(&Service::id);

  return ioc.service_registry_->template use_service<Service>(ioc);
}

template <>
inline detail::io_context_impl& use_service<detail::io_context_impl>(
    io_context_runner& ioc)
{
  return ioc.impl_;
}

} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#if defined(NET_TS_HAS_IOCP)
# include <experimental/__net_ts/detail/win_iocp_io_context.hpp>
#else
# include <experimental/__net_ts/detail/scheduler.hpp>
#endif

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {

inline io_context_runner::executor_type
io_context_runner::get_executor() NET_TS_NOEXCEPT
{
  return io_context_runner::executor_type(*this);
}

#if defined(NET_TS_HAS_CHRONO)

template <typename Rep, typename Period>
std::size_t io_context_runner::run_for(
    const chrono::duration<Rep, Period>& rel_time)
{
  return this->run_until(chrono::steady_clock::now() + rel_time);
}

template <typename Clock, typename Duration>
std::size_t io_context_runner::run_until(
    const chrono::time_point<Clock, Duration>& abs_time)
{
  std::size_t n = 0;
  while (this->run_one_until(abs_time))
    if (n != (std::numeric_limits<std::size_t>::max)())
      ++n;
  return n;
}

template <typename Rep, typename Period>
std::size_t io_context_runner::run_one_for(
    const chrono::duration<Rep, Period>& rel_time)
{
  return this->run_one_until(chrono::steady_clock::now() + rel_time);
}

template <typename Clock, typename Duration>
std::size_t io_context_runner::run_one_until(
    const chrono::time_point<Clock, Duration>& abs_time)
{
  typename Clock::time_point now = Clock::now();
  while (now < abs_time)
  {
    typename Clock::duration rel_time = abs_time - now;
    if (rel_time > chrono::seconds(1))
      rel_time = chrono::seconds(1);

    std::error_code ec;
    std::size_t s = impl_.wait_one(
        static_cast<long>(chrono::duration_cast<
          chrono::microseconds>(rel_time).count()), ec);
    std::experimental::net::v1::detail::throw_error(ec);

    if (s || impl_.stopped())
      return s;

    now = Clock::now();
  }

  return 0;
}

#endif // defined(NET_TS_HAS_CHRONO)

inline io_context_runner&
io_context_runner::executor_type::context() const NET_TS_NOEXCEPT
{
  return io_context_;
}

inline void
io_context_runner::executor_type::on_work_started() const NET_TS_NOEXCEPT
{
  io_context_.impl_.work_started();
}

inline void
io_context_runner::executor_type::on_work_finished() const NET_TS_NOEXCEPT
{
  io_context_.impl_.work_finished();
}

template <typename Function, typename Allocator>
void io_context_runner::executor_type::dispatch(
    NET_TS_MOVE_ARG(Function) f, const Allocator& a) const
{
  typedef typename decay<Function>::type function_type;

  // Invoke immediately if we are already inside the thread pool.
  if (io_context_.impl_.can_dispatch())
  {
    // Make a local, non-const copy of the function.
    function_type tmp(NET_TS_MOVE_CAST(Function)(f));

    detail::fenced_block b(detail::fenced_block::full);
    networking_ts_handler_invoke_helpers::invoke(tmp, tmp);
    return;
  }

  // Allocate and construct an operation to wrap the function.
  typedef detail::executor_op<function_type, Allocator, detail::operation> op;
  typename op::ptr p = { detail::addressof(a), op::ptr::allocate(a), 0 };
  p.p = new (p.v) op(NET_TS_MOVE_CAST(Function)(f), a);

  NET_TS_HANDLER_CREATION((this->context(), *p.p,
        "io_context", &this->context(), 0, "post"));

  io_context_.impl_.post_immediate_completion(p.p, false);
  p.v = p.p = 0;
}

template <typename Function, typename Allocator>
void io_context_runner::executor_type::post(
    NET_TS_MOVE_ARG(Function) f, const Allocator& a) const
{
  typedef typename decay<Function>::type function_type;

  // Allocate and construct an operation to wrap the function.
  typedef detail::executor_op<function_type, Allocator, detail::operation> op;
  typename op::ptr p = { detail::addressof(a), op::ptr::allocate(a), 0 };
  p.p = new (p.v) op(NET_TS_MOVE_CAST(Function)(f), a);

  NET_TS_HANDLER_CREATION((this->context(), *p.p,
        "io_context", &this->context(), 0, "post"));

  io_context_.impl_.post_immediate_completion(p.p, false);
  p.v = p.p = 0;
}

template <typename Function, typename Allocator>
void io_context_runner::executor_type::defer(
    NET_TS_MOVE_ARG(Function) f, const Allocator& a) const
{
  typedef typename decay<Function>::type function_type;

  // Allocate and construct an operation to wrap the function.
  typedef detail::executor_op<function_type, Allocator, detail::operation> op;
  typename op::ptr p = { detail::addressof(a), op::ptr::allocate(a), 0 };
  p.p = new (p.v) op(NET_TS_MOVE_CAST(Function)(f), a);

  NET_TS_HANDLER_CREATION((this->context(), *p.p,
        "io_context", &this->context(), 0, "defer"));

  io_context_.impl_.post_immediate_completion(p.p, true);
  p.v = p.p = 0;
}

inline bool
io_context_runner::executor_type::running_in_this_thread() const NET_TS_NOEXCEPT
{
  return io_context_.impl_.can_dispatch();
}

inline std::experimental::net::v1::io_context& io_context::service::get_io_context()
{
  return static_cast<std::experimental::net::v1::io_context&>(context());
}

struct io_context::executor_type {
  /// Obtain the underlying execution context.
  io_context &context() const noexcept { return io; }

  void on_work_started() const noexcept {
    // TODO: forward to the underlying implementation
    throw std::logic_error("not yet implemented");
    //if (io.get_meta() == 0) io.impl.io->impl_.work_started();
   // else io.impl.tp->scheduler().work_started();
  }

  void on_work_finished() const noexcept {
    throw std::logic_error("not yet implemented");
    // TODO:  tp.impl.work_finished();
  }

  template <typename Function, typename Allocator>
  void dispatch(NET_TS_MOVE_ARG(Function) f, const Allocator &a) const {
    throw std::logic_error("not yet implemented");
  }

  template <typename Function, typename Allocator>
  void post(Function && f, Allocator const& a) const {
    throw std::logic_error("not yet implemented");
  }
  template <typename Function, typename Allocator>
  void defer(NET_TS_MOVE_ARG(Function) f, const Allocator &a) const {
    throw std::logic_error("not yet implemented");
  }

  /// Compare two executors for equality.
  /**
  * Two executors are equal if they refer to the same underlying io_context.
  */
  friend bool operator==(const executor_type &a,
    const executor_type &b) NET_TS_NOEXCEPT {
    return &a.io == &b.io;
  }

  /// Compare two executors for inequality.
  /**
  * Two executors are equal if they refer to the same underlying io_context.
  */
  friend bool operator!=(const executor_type &a,
    const executor_type &b) NET_TS_NOEXCEPT {
    return !(a == b);
  }

private:
  friend class io_context;

  // Constructor.
  explicit executor_type(io_context &io) : io(io) {}

  // The underlying io_context.
  io_context &io;
};

/// Obtains the executor associated with the io_context.
io_context::executor_type io_context::get_executor() NET_TS_NOEXCEPT {
  return executor_type{*this};
}

} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_IMPL_IO_CONTEXT_HPP
