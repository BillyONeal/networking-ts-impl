//
// detail/impl/basic_scheduler.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright Microsoft Corporation
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_BASIC_SCHEDULER_HPP
#define NET_TS_DETAIL_BASIC_SCHEDULER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>
#include <experimental/__net_ts/detail/atomic_count.hpp>
#include <experimental/__net_ts/detail/thread_context.hpp>
#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

template<class OperationType>
struct basic_scheduler
  : public thread_context,
  public execution_context_service_base<basic_scheduler<OperationType>>
{
  using operation = OperationType;

  basic_scheduler() = default;
  using execution_context_service_base<basic_scheduler<OperationType>>::execution_context_service_base;
  basic_scheduler(const basic_scheduler&) = delete;
  basic_scheduler& operator=(const basic_scheduler&) = delete;
  virtual ~basic_scheduler() = default;

  virtual void shutdown() NET_TS_NOEXCEPT = 0;
  virtual void stop() NET_TS_NOEXCEPT = 0;
  virtual bool stopped() const NET_TS_NOEXCEPT = 0;
  virtual bool concurrency_hint_is_locking() const NET_TS_NOEXCEPT { return true; }
  virtual void post_immediate_completion(operation* op, bool is_continuation) = 0;
  virtual void post_deferred_completion(operation* op) = 0;
  virtual void post_deferred_completions(op_queue<operation>& ops) = 0;
  virtual void do_dispatch(operation* op) = 0;
  virtual void abandon_operations(op_queue<operation>& ops) = 0;

  // Notify that some work has started.
  void work_started() NET_TS_NOEXCEPT
  {
    ++outstanding_work_;
  }

  // Notify that some work has finished.
  void work_finished() NET_TS_NOEXCEPT
  {
    if (--outstanding_work_ == 0)
      stop();
  }

  // Return whether a handler can be dispatched immediately.
  bool can_dispatch()
  {
    return thread_call_stack::contains(this) != 0;
  }

protected:
  // The count of unfinished work.
  atomic_count outstanding_work_ = 0;
};

}
}
}
}
}

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_BASIC_SCHEDULER_HPP
