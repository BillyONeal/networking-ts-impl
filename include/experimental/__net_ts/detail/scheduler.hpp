//
// detail/scheduler.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_SCHEDULER_HPP
#define NET_TS_DETAIL_SCHEDULER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>
#include <experimental/__net_ts/detail/basic_scheduler.hpp>

#include <system_error>
#include <experimental/__net_ts/execution_context.hpp>
#include <experimental/__net_ts/detail/atomic_count.hpp>
#include <experimental/__net_ts/detail/conditionally_enabled_event.hpp>
#include <experimental/__net_ts/detail/conditionally_enabled_mutex.hpp>
#include <experimental/__net_ts/detail/op_queue.hpp>
#include <experimental/__net_ts/detail/reactor_fwd.hpp>
#include <experimental/__net_ts/detail/scheduler_operation.hpp>
#include <experimental/__net_ts/detail/thread_context.hpp>
#include <experimental/__net_ts/detail/concurrency_hint.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

struct scheduler_thread_info;

class scheduler
  : public basic_scheduler<scheduler_operation>
{
public:
  // Constructor. Specifies the number of concurrent threads that are likely to
  // run the scheduler. If set to 1 certain optimisation are performed.
  NET_TS_DECL scheduler(std::experimental::net::v1::execution_context& ctx,
      int concurrency_hint = 0);

  // Destroy all user-defined handler objects owned by the service.
  NET_TS_DECL void shutdown() NET_TS_NOEXCEPT override;

  // Initialise the task, if required.
  NET_TS_DECL void init_task();

  // Run the event loop until interrupted or no more work.
  NET_TS_DECL std::size_t run(std::error_code& ec);

  // Run until interrupted or one operation is performed.
  NET_TS_DECL std::size_t run_one(std::error_code& ec);

  // Run until timeout, interrupted, or one operation is performed.
  NET_TS_DECL std::size_t wait_one(
      long usec, std::error_code& ec);

  // Poll for operations without blocking.
  NET_TS_DECL std::size_t poll(std::error_code& ec);

  // Poll for one operation without blocking.
  NET_TS_DECL std::size_t poll_one(std::error_code& ec);

  // Interrupt the event processing loop.
  NET_TS_DECL void stop() NET_TS_NOEXCEPT;

  // Determine whether the scheduler is stopped.
  NET_TS_DECL bool stopped() const NET_TS_NOEXCEPT;

  // Restart in preparation for a subsequent run invocation.
  NET_TS_DECL void restart();

  // Notify that some work has started.
  void work_started()
  {
    ++outstanding_work_;
  }

  // Used to compensate for a forthcoming work_finished call. Must be called
  // from within a scheduler-owned thread.
  NET_TS_DECL void compensating_work_started();

  // Notify that some work has finished.
  void work_finished()
  {
    if (--outstanding_work_ == 0)
      stop();
  }

  // Request invocation of the given operation and return immediately. Assumes
  // that work_started() has not yet been called for the operation.
  NET_TS_DECL void post_immediate_completion(
      operation* op, bool is_continuation) override;

  // Request invocation of the given operation and return immediately. Assumes
  // that work_started() was previously called for the operation.
  NET_TS_DECL void post_deferred_completion(operation* op) override;

  // Request invocation of the given operations and return immediately. Assumes
  // that work_started() was previously called for each operation.
  NET_TS_DECL void post_deferred_completions(op_queue<operation>& ops) override;

  // Enqueue the given operation following a failed attempt to dispatch the
  // operation for immediate invocation.
  NET_TS_DECL void do_dispatch(operation* op) override;

  // Process unfinished operations as part of a shutdownoperation. Assumes that
  // work_started() was previously called for the operations.
  NET_TS_DECL void abandon_operations(op_queue<operation>& ops);

  // Get the concurrency hint that was used to initialise the scheduler.
  bool concurrency_hint_is_locking() const NET_TS_NOEXCEPT override
  {
    return NET_TS_CONCURRENCY_HINT_IS_LOCKING(SCHEDULER, concurrency_hint_);
  }

private:
  // The mutex type used by this scheduler.
  typedef conditionally_enabled_mutex mutex;

  // The event type used by this scheduler.
  typedef conditionally_enabled_event event;

  // Structure containing thread-specific data.
  typedef scheduler_thread_info thread_info;

  // Run at most one operation. May block.
  NET_TS_DECL std::size_t do_run_one(mutex::scoped_lock& lock,
      thread_info& this_thread, const std::error_code& ec);

  // Run at most one operation with a timeout. May block.
  NET_TS_DECL std::size_t do_wait_one(mutex::scoped_lock& lock,
      thread_info& this_thread, long usec, const std::error_code& ec);

  // Poll for at most one operation.
  NET_TS_DECL std::size_t do_poll_one(mutex::scoped_lock& lock,
      thread_info& this_thread, const std::error_code& ec);

  // Stop the task and all idle threads.
  NET_TS_DECL void stop_all_threads(mutex::scoped_lock& lock);

  // Wake a single idle thread, or the task, and always unlock the mutex.
  NET_TS_DECL void wake_one_thread_and_unlock(
      mutex::scoped_lock& lock);

  // Helper class to perform task-related operations on block exit.
  struct task_cleanup;
  friend struct task_cleanup;

  // Helper class to call work-related operations on block exit.
  struct work_cleanup;
  friend struct work_cleanup;

  // Whether to optimise for single-threaded use cases.
  const bool one_thread_;

  // Mutex to protect access to internal data.
  mutable mutex mutex_;

  // Event to wake up blocked threads.
  event wakeup_event_;

  // The task to be run by this service.
  reactor* task_;

  // Operation object to represent the position of the task in the queue.
  struct task_operation : operation
  {
    task_operation() : operation(0) {}
  } task_operation_;

  // Whether the task has been interrupted.
  bool task_interrupted_;

  // The count of unfinished work.
  atomic_count outstanding_work_;

  // The queue of handlers that are ready to be delivered.
  op_queue<operation> op_queue_;

  // Flag to indicate that the dispatcher has been stopped.
  bool stopped_;

  // Flag to indicate that the dispatcher has been shut down.
  bool shutdown_;

  // The concurrency hint used to initialise the scheduler.
  const int concurrency_hint_;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#if defined(NET_TS_HEADER_ONLY)
# include <experimental/__net_ts/detail/impl/scheduler.ipp>
#endif // defined(NET_TS_HEADER_ONLY)

#endif // NET_TS_DETAIL_SCHEDULER_HPP
