//
// detail/simple_intrusive_list.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 No copyright claimed
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_SIMPLE_INTRUSIVE_LIST_HPP
#define NET_TS_DETAIL_SIMPLE_INTRUSIVE_LIST_HPP

// FIXME: reconcile with object_pool

#include <cassert>

struct simple_intrusive_list_base {
  struct entry {
    entry *next;
    entry *prev;

    bool erase_and_check_if_list_is_empty() {
      entry *const prev_entry = this->prev;
      entry *const next_entry = this->next;
      prev_entry->next = next_entry;
      next_entry->prev = prev_entry;
      this->prev = this->next = nullptr;
      return next_entry == prev_entry;
    }
  };
  entry head;

  simple_intrusive_list_base(simple_intrusive_list_base const &) = delete;
  simple_intrusive_list_base(simple_intrusive_list_base &&) = delete;

  simple_intrusive_list_base() { clear(); }
  bool empty() const { return head.next == &head; }

  void clear() { head.next = head.prev = &head; }
  bool is_head(entry *e) const { return e == &head; }

  entry *pop_front() {
    entry *const front = head.next;
    entry *const next = front->next;
    head.next = next;
    next->prev = &head;
    return front;
  }

  entry *pop_back() {
    entry *const back = head.prev;
    entry *const prev = back->prev;
    head.prev = prev;
    prev->next = &head;
    return back;
  }

  void push_back(entry *new_entry) {
    entry *const prev = head.prev;
    new_entry->next = &head;
    new_entry->prev = prev;
    prev->next = new_entry;
    head.prev = new_entry;
  }

  void push_front(entry *new_entry) {
    entry *const next = head.next;
    new_entry->next = next;
    new_entry->prev = &head;
    next->prev = new_entry;
    head.next = new_entry;
  }
};

using simple_intrusive_list_entry = simple_intrusive_list_base::entry;

template <typename T> struct simple_intrusive_stack {
  simple_intrusive_list_entry *root = nullptr;

  simple_intrusive_stack() = default;
  simple_intrusive_stack(simple_intrusive_stack const &) = delete;
  simple_intrusive_stack(simple_intrusive_stack &&rhs) : root(rhs.root) {
    rhs.root = nullptr;
  }
  simple_intrusive_stack &operator=(simple_intrusive_stack const &) = delete;
  simple_intrusive_stack &operator=(simple_intrusive_stack &&rhs) {
    if (std::addressof(rhs) != this) {
      assert(root == nullptr);
      root = rhs.root;
      rhs.root = nullptr;
    }
    return *this;
  }

  ~simple_intrusive_stack() { assert(root == nullptr); }

  void push(T *x) {
    x->next = root;
    root = x;
  }

  T *try_pop() {
    if (auto result = root) {
      root = result->next;
      return static_cast<T *>(result);
    }
    return nullptr;
  }
};

template <typename T>
struct simple_intrusive_list : public simple_intrusive_list_base {
  using list_base = simple_intrusive_list_base;
  T *pop_front() {
    auto e = list_base::pop_front();
    return static_cast<T *>(e);
  }
  T *pop_back() {
    auto e = list_base::pop_back();
    return static_cast<T *>(e);
  }
  T *pop_front_or_null() {
    auto e = list_base::pop_front();
    if (is_head(e))
      return nullptr;
    return static_cast<T *>(e);
  }
  T *pop_back_or_null() {
    entry *e = list_base::pop_back();
    if (is_head(e))
      return nullptr;
    return static_cast<T *>(e);
  }

  template <typename Pred> simple_intrusive_stack<T> extract_if(Pred p) {
    simple_intrusive_stack<T> result;
    auto *current = head.next;
    while (current != &head) {
      auto *next_item = current->next;
      T *value = static_cast<T *>(current);
      if (p(value)) {
        value->erase_and_check_if_list_is_empty();
        result.push(value);
      }
      current = next_item;
    }
    return result;
  }

  ~simple_intrusive_list() {
    assert(empty() && "expect list to be empty on destruction");
  }

  struct iterator {
    entry *e;

    T *operator*() { return static_cast<T *>(e); }
    iterator operator++() {
      e = e->next;
      return *this;
    }
    iterator operator++(int) {
      iterator result{e};
      e = e->next;
      return result;
    }
    bool operator!=(iterator rhs) { return e != rhs.e; }
    bool operator==(iterator rhs) { return e == rhs.e; }
  };

  iterator begin() { return iterator{head.next}; }
  iterator end() { return iterator{&head}; }
};

#endif