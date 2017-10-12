//
// detail/wintp_scheduler.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_WINTP_SCHEDULER_HPP
#define NET_TS_DETAIL_WINTP_SCHEDULER_HPP

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
#include <experimental/__net_ts/detail/operation.hpp>
#include <experimental/__net_ts/detail/op_queue.hpp>
#include <experimental/__net_ts/detail/op_counter.hpp>
#include <experimental/__net_ts/detail/wintp_mutex.hpp>
#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

struct wintp_scheduler : public service_base<wintp_scheduler> {
  wintp_scheduler(io_context &io, PTP_CALLBACK_ENVIRON env = nullptr)
      : service_base(io), env(nullptr),
        work(CreateThreadpoolWork(
            [](auto, void *ctx, auto) {
              static_cast<wintp_scheduler *>(ctx)->deque_and_run();
            },
            this, env)) {
    if (!work)
      throw std::bad_alloc();
  }

  // Notify that some work has started.
  void work_started() {
    if (!counter.try_inc())
      throw std::logic_error("context being cancelled");
  }

  // Notify that some work has finished.
  void work_finished() { counter.deref(); }

private:
  void deque_and_run() {
    operation *op = nullptr;
    {
      std::lock_guard<wintp_mutex> grab(lock);
      op = ops.try_pop();
    }
    if (op) {
      auto* err_cat = reinterpret_cast<std::error_category*>(op->Internal);
      auto err_value = op->Offset;
      std::error_code result_ec(err_value, *err_cat);
      auto bytes_transferred = op->OffsetHigh;

      op->complete(this, result_ec, bytes_transferred);
      counter.deref(); // TOOD: exception guard
    }
  }

public:
  void reserved_post(operation *op) {
    {
      std::lock_guard<wintp_mutex> grab(lock);
      ops.push(op);
    }
    SubmitThreadpoolWork(work);
  }

  template <typename OtherOperation>
  void reserved_post(op_queue<OtherOperation> &q) {
    size_t count = 0;
    q.for_each([&count](auto*) { ++count; });

    {
      std::lock_guard<wintp_mutex> grab(lock);
      ops.push(q);
    }

    for (size_t i = 0; i < count; ++i)
      SubmitThreadpoolWork(work);
  }

  void post(operation *op) {
    work_started();
    reserved_post(op);
  }

private:
  std::mutex join_mut;
  std::condition_variable join_cv;
  bool join_done = false;
public:

  void join() {
    counter.try_set_done_callback([this] {
      puts("done");
      {
        std::lock_guard<std::mutex> grab(join_mut);
        join_done = true;
      }
      join_cv.notify_all();
    });
    std::unique_lock<std::mutex> lk(join_mut);
    if (join_done)
      return;
    join_cv.wait(lk, [this] { return join_done; });
  }

  ~wintp_scheduler() {
    // FIXME: check for all outstanding work, not just what is currently
    // running.
    WaitForThreadpoolWorkCallbacks(work, /*cancel=*/true);
    CloseThreadpoolWork(work);
  }
private:
  PTP_CALLBACK_ENVIRON env;
  PTP_WORK work;
  op_counter counter;
  wintp_mutex lock;
  op_queue<operation> ops;
  std::error_code result_ec;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_DEADLINE_TIMER_SERVICE_HPP
