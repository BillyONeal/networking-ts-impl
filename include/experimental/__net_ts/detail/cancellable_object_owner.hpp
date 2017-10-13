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
#include <experimental/__net_ts/detail/wintp_mutex.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace detail {

// TODO: integrate with object_pool

struct cancellable_object_base {
  friend class object_pool_access;

  virtual ~cancellable_object_base() {}

  bool try_set_cancel_pending(char const* name) {
    auto oldValue =
        state.fetch_and(~NOT_BEING_CANCELLED, std::memory_order_acq_rel);
    bool firstToCancel = (oldValue & NOT_BEING_CANCELLED) != 0;
    printf("%s: first-to-cancel %d\n", name, firstToCancel);
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
    printf("%s: deref => %d\n", name, newValue / REF);
    return (newValue == 0);
  }

  template <typename Object>
  void drop_ref(char const* name, object_pool<Object>& pool) {
    if (drop_ref_internal(name)) {
      pool.free(static_cast<Object*>(this));
    }
  }

private:
  enum : unsigned {
    NOT_BEING_CANCELLED = 1,
    REF = 2,
  };

  cancellable_object_base *next = nullptr;
  cancellable_object_base *prev = nullptr;

  // Initially have two references. One for the handle to an object,
  // another for being a member of the children list in the owner.
  std::atomic<unsigned> state = NOT_BEING_CANCELLED + 2 * REF;
};

template <typename Object>
struct cancellabl_object_owner
{
  void ParentCancel() {
    auto itemsToCancel = extractItemsThatNeedCancelling();
    while (T* item = itemsToCancel.try_pop()) {
      item->InitiateCancel();
      item->DropRef("parent");
    }
  }

  void ChildAdd(T* item) noexcept {
    bool needCancel = false;
    {
      std::lock_guard<std::mutex> grab(lock);
      if (cancelRequested)
        needCancel = true;
      else
        list.push_back(item);
    }
    if (needCancel) {
      item->InitiateCancel();
      item->DropRef("owner");
    }
  }

  void ChildRemove(T* item) {
    {
      std::lock_guard<std::mutex> grab(lock);
      item->erase_and_check_if_list_is_empty();
    }
    item->DropRef("owner", pool);
  }

  ~cancellabl_object_owner() { puts("OwnerOf: dtor"); }

private:
  intrusive_stack<T> extractItemsThatNeedCancelling() {
    std::lock_guard<std::mutex> grab(lock);
    if (cancelRequested)
      return {};
    cancelRequested = true;
    return list.extract_if(
      [](auto *item) { return item->TrySetCancelPending("paren"); });
  }

private:
  wintp_mutex lock;
  bool cancelRequested = false;

  object_pool<Object> pool;
};

} // namespace detail
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_DETAIL_OBJECT_POOL_HPP
