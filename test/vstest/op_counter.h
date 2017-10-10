#ifndef OP_COUNTER
#define OP_COUNTER

#include <stdio.h>
#include "win_mutex.h"
#include <mutex>
#include <functional>
#include <atomic>

struct op_counter {
  op_counter(std::function<void()> cancelFn = [] {}) : fnCancel(cancelFn) {}

  void cancel() {
    if (try_set_cancel())
      fnCancel();
  }

  bool try_inc() {
    if (counter.fetch_add(INC, std::memory_order_acq_rel) & NO_CANCEL) {
      dump("try_inc");
      return true;
    }

    // we are being cancelled. Undo the increment
    deref();
    return false;
  }

  void deref() {
    post_deref_check_for_cancel(
      counter.fetch_sub(INC, std::memory_order_acq_rel) - INC);
  }

  bool try_set_done_callback(std::function<void()> done) {
    {
      std::lock_guard<win_mutex> lock(m);
      if (this->fnDone)
        return false;
      this->fnDone = std::move(done);
    }
    // Safe to do subtract, because only one thread will be able to set
    // fnDone.
    post_deref_check_for_cancel(
      counter.fetch_sub(NO_JOINER, std::memory_order_acq_rel) - NO_JOINER);
    return true;
  }
private:
  enum : uint64_t {
    NO_JOINER = 1,
    NO_CANCEL = 2,
    INFORMED = 4,
    INC = 8,
  };

#if 0
  void dump(const char* label) {
    auto value = counter.load();
    printf("cnt.%s => %llu ", label, value / INC);
    if ((value & NO_CANCEL) == 0) putchar('C');
    if ((value & NO_JOINER) == 0) putchar('J');
    if (value & INFORMED) putchar('i');
    putchar('\n');
  }
#else
  void dump(const char*) {}
#endif

  // Sets cancellation flag. Returns true if first to set the flag.
  bool try_set_cancel() {
    return 0 != (counter.fetch_and(~NO_CANCEL, std::memory_order_acq_rel) &
      NO_CANCEL);
  }

  // Sets join flag. Returns true if first to set the flag.
  bool try_set_join() {
    return 0 != (counter.fetch_and(~NO_JOINER, std::memory_order_acq_rel) &
      NO_JOINER);
  }

  void post_deref_check_for_inform(uint64_t value) {
    // See if we need to inform that we are done
    if (value == 0) {
      value = counter.fetch_or(INFORMED, std::memory_order_acq_rel);
      // Am I the first one to set INFORMED bit.
      if (value == 0) {
        std::lock_guard<win_mutex> grab(m);
        fnDone();
      }
    }
  }

  void post_deref_check_for_cancel(uint64_t value) {
    dump("deref");

    // See if we need to cancel.
    if (value == NO_CANCEL) {
      value = counter.fetch_and(~NO_CANCEL, std::memory_order_acq_rel);
      if ((value & NO_CANCEL) != 0) {
        fnCancel();
      }
      value &= ~NO_CANCEL;
    }

    post_deref_check_for_inform(value);
  }

  std::function<void()> fnCancel;
  win_mutex m;
  std::function<void()> fnDone;
  std::atomic<uint64_t> counter = NO_JOINER | NO_CANCEL;
};

#endif