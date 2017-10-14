//
// detail/cancellable_object_owner.hpp
// ~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_CANCELLABLE_OBJECT_OWNER_HPP
#define NET_TS_DETAIL_CANCELLABLE_OBJECT_OWNER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <atomic>

#include <experimental/__net_ts/detail/noncopyable.hpp>
#include <experimental/__net_ts/detail/object_pool.hpp>
#include <experimental/__net_ts/detail/simple_intrusive_list.hpp>
#include <experimental/__net_ts/detail/wintp_mutex.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

// TODO: integrate with object_pool

struct cancellable_object_base : simple_intrusive_list_entry {

  virtual ~cancellable_object_base() {}

  bool try_set_cancel_pending(char const* name) {
    auto oldValue =
        state.fetch_and(~NOT_BEING_CANCELLED, std::memory_order_acq_rel);
    bool firstToCancel = (oldValue & NOT_BEING_CANCELLED) != 0;
    //printf("%s: first-to-cancel %d\n", name, firstToCancel);
    return firstToCancel;
  }

  bool try_increment_local_io() {
    auto old = state.load(std::memory_order_relaxed);
    auto newValue = old + REF;
    do {
      if ((old & NOT_BEING_CANCELLED) == 0)
        return false;
      newValue = old + REF;
    } while (state.compare_exchange_weak(
      old, newValue, std::memory_order_release, std::memory_order_relaxed));
    return true;
  }

  void increment_local_io() {
    if (!try_increment_local_io())
      throw std::logic_error("tp_object is being canceled");
  }

  // returns true if no more refs
  bool drop_ref_internal(char const* name) {
    auto newValue = state.fetch_sub(REF, std::memory_order_acq_rel) - REF;
    //printf("%s: deref => %d\n", name, newValue / REF);
    return (newValue == 0);
  }

  void drop_ref(char const* name) {
    if (drop_ref_internal(name)) {
      delete this; // TODO: hookup object pool
    }
  }

private:
  enum : unsigned {
    NOT_BEING_CANCELLED = 1,
    REF = 2,
  };

  // Initially have two references. One for the handle to an object,
  // another for being a member of the children list in the owner.
  std::atomic<unsigned> state = NOT_BEING_CANCELLED + 2 * REF;
};

template <typename T>
struct cancellable_object_owner
{
  bool cancel_all(const char* label = "parent") {
    bool already_requested = false;
    auto itemsToCancel = extractItemsThatNeedCancelling(label, already_requested);
    if (already_requested)
      return false;

    while (T* item = itemsToCancel.try_pop()) {
      item->initiate_cancel();
      item->drop_ref(label);
    }
    return true;
  }

  void add_child(T* item) noexcept {
    bool needCancel = false;
    {
      std::lock_guard<wintp_mutex> grab(lock);
      if (cancelRequested)
        needCancel = true;
      else
        children.push_back(item);
    }
    if (needCancel) {
      item->initiate_cancel();
      item->drop_ref("owner");
    }
  }

  void remove_child(T *item) {
    {
      std::lock_guard<wintp_mutex> grab(lock);
      item->erase_and_check_if_list_is_empty();
    }
    item->drop_ref("owner");
  }

  //~cancellable_object_owner() { puts("OwnerOf: dtor"); }

  bool cancelled() const {
    std::lock_guard<wintp_mutex> grab(lock);
    return cancelRequested;
  }

private:
  simple_intrusive_stack<T>
  extractItemsThatNeedCancelling(const char *label, bool &already_requested) {
    std::lock_guard<wintp_mutex> grab(lock);
    if (cancelRequested) {
      already_requested = true;
      return {};
    }
    already_requested = false;
    cancelRequested = true;
    return children.extract_if(
      [label](auto *item) { return item->try_set_cancel_pending(label); });
  }

private:
  mutable wintp_mutex lock;
  bool cancelRequested = false;

  simple_intrusive_list<T> children;
  // TODO: integrate with object_pool
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_OBJECT_POOL_HPP
