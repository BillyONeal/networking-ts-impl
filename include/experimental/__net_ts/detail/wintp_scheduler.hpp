//
// detail/wintp_scheduler.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright Microsoft Corporation, All Rights Reserved.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_WIN_TP_SCHEDULER_HPP
#define NET_TS_DETAIL_WIN_TP_SCHEDULER_HPP

#pragma once
#pragma comment(lib, "synchronization")

#include <experimental/__net_ts/detail/config.hpp>
#include <intrin.h>
#include <assert.h>

#include <experimental/__net_ts/detail/win_iocp_operation.hpp>
#include <experimental/__net_ts/execution_context.hpp>
#include <experimental/__net_ts/detail/basic_scheduler.hpp>
#include <Windows.h>
#include <shared_mutex>
#include <mutex>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

class wintp_scheduler final
  : public basic_scheduler<win_iocp_operation>
{
  static void __stdcall threadpool_work_callback(PTP_CALLBACK_INSTANCE, void * const this_raw, PTP_WORK) noexcept {
    auto* this_ = static_cast<wintp_scheduler*>(this_raw);
    win_iocp_operation * retiredOp;
    {
      lock_guard lck(this_->mtx_);
      retiredOp = this_->pending_.front();
      this_->pending_.pop();
    }
      
    auto* err_cat = reinterpret_cast<std::error_category*>(retiredOp->Internal);
    auto err_value = retiredOp->Offset;
    std::error_code result_ec(err_value, *err_cat);
    auto bytes_transferred = retiredOp->OffsetHigh;

    retiredOp->complete(this_, result_ec, bytes_transferred);
  }

public:
  wintp_scheduler(std::experimental::net::v1::execution_context& ctx, PTP_CALLBACK_ENVIRON env)
    : basic_scheduler<win_iocp_operation>(ctx, true)
    , env_(env)
    , work_(CreateThreadpoolWork(threadpool_work_callback, this, env))
    , mtx_() {
    if (!work_) {
      throw std::bad_alloc{};
    }
  }

  ~wintp_scheduler() NET_TS_NOEXCEPT {
    shutdown();
    CloseThreadpoolWork(work_);
  }

  void shutdown() NET_TS_NOEXCEPT override { // may only be called by 1 thread
    WaitForThreadpoolWorkCallbacks(work_, TRUE);
    while (win_iocp_operation* op = pending_.front()) {
      pending_.pop();
      op->destroy();
    }
  }

  void stop() NET_TS_NOEXCEPT override {
    WakeByAddressAll(&outstanding_work_);
  }

  void cancel_all() NET_TS_NOEXCEPT {
  }

  void join() NET_TS_NOEXCEPT {
    long outstanding_work;
    static_assert(sizeof(outstanding_work) == sizeof(outstanding_work_));
    while (outstanding_work = outstanding_work_.load(), outstanding_work != 0) {
      WaitOnAddress(&outstanding_work_, &outstanding_work, sizeof(outstanding_work_), INFINITE);
    }

    WaitForThreadpoolWorkCallbacks(work_, FALSE);
  }

  virtual void post_immediate_completion(win_iocp_operation* op, bool) { post_deferred_completion(op); }
  virtual void post_deferred_completion(win_iocp_operation* op) {
    {
      lock_guard lck(mtx_);
      pending_.push(op);
    }

    SubmitThreadpoolWork(work_);
  }

  virtual void post_deferred_completions(op_queue<win_iocp_operation>& ops) {
    long posts = ops.count();
    {
      lock_guard lck(mtx_);
      pending_.push(ops);
    }

    for (; 0 < posts; --posts) {
      SubmitThreadpoolWork(work_);
    }
  }

private:
  const PTP_CALLBACK_ENVIRON env_;
  const PTP_WORK work_;
  op_queue<win_iocp_operation> pending_; // guarded by mtx_, TODO use lock free MPMC queue
  shared_mutex mtx_;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_WIN_TP_SCHEDULER_HPP
