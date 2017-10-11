//
// basic_io_object.hpp
// ~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_BASIC_IO_OBJECT_HPP
#define NET_TS_BASIC_IO_OBJECT_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>
#include <experimental/__net_ts/io_context.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
  inline namespace v1 {

#if defined(NET_TS_HAS_MOVE)
    namespace detail
    {
      // Type trait used to determine whether a service supports move.
      template <typename IoObjectService>
      class service_has_move
      {
      private:
        typedef IoObjectService service_type;
        typedef typename service_type::implementation_type implementation_type;

        template <typename T, typename U>
        static auto asio_service_has_move_eval(T* t, U* u)
          -> decltype(t->move_construct(*u, *u), char());
        static char(&asio_service_has_move_eval(...))[2];

      public:
        static const bool value =
          sizeof(asio_service_has_move_eval(
            static_cast<service_type*>(0),
            static_cast<implementation_type*>(0))) == 1;
      };
    }
#endif // defined(NET_TS_HAS_MOVE)

#define NET_TS_SVC_INVOKE_(name) \
  ((this->get_meta() == 0) \
   ? this->get_service1().name(this->get_implementation1()) \
   : this->get_service2().name(this->get_implementation2()))

#define NET_TS_SVC_INVOKE(name, ...) \
  ((this->get_meta() == 0) \
   ? this->get_service1().name(this->get_implementation1(), __VA_ARGS__) \
   : this->get_service2().name(this->get_implementation2(), __VA_ARGS__))


//  if (index == 0) svc.s1->name(impl.impl1, SVC_UNPACK(args));
//  else svc.s2->name(impl.impl2, SVC_UNPACK(args))

/// Base class for all I/O objects.
/**
 * @note All I/O objects are non-copyable. However, when using C++0x, certain
 * I/O objects do support move construction and move assignment.
 */
#if !defined(NET_TS_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)
template <typename IoObjectService>
#else
template <typename IoObjectService1, typename IoObjectService2 = IoObjectService1,
    bool Movable = detail::service_has_move<IoObjectService1>::value>
#endif
class basic_io_object
{
public:
  using service_type1 = IoObjectService1;
  using service_type2 = IoObjectService2;
  /// The type of the service that will be used to provide I/O operations.
  union service_type {
    IoObjectService1* svc1;
    IoObjectService2* svc2;
  };

  /// The underlying implementation type of I/O object.
  union implementation_type {
    typename IoObjectService1::implementation_type impl1;
    typename IoObjectService2::implementation_type impl2;
    implementation_type() {}
    ~implementation_type() {}
  };

  /// The type of the executor associated with the object.
  typedef std::experimental::net::io_context::executor_type executor_type;

  /// Get the executor associated with the object.
  executor_type get_executor() NET_TS_NOEXCEPT
  {
    return service_.get_io_context().get_executor();
  }

protected:
  /// Construct a basic_io_object.
  /**
   * Performs:
   * @code get_service().construct(get_implementation()); @endcode
   */
  explicit basic_io_object(std::experimental::net::io_context& io_context)
    : service_(std::experimental::net::use_service<IoObjectService>(io_context))
  {
    service_.construct(implementation_);
  }

#if defined(GENERATING_DOCUMENTATION)
  /// Move-construct a basic_io_object.
  /**
   * Performs:
   * @code get_service().move_construct(
   *     get_implementation(), other.get_implementation()); @endcode
   *
   * @note Available only for services that support movability,
   */
  basic_io_object(basic_io_object&& other);

  /// Move-assign a basic_io_object.
  /**
   * Performs:
   * @code get_service().move_assign(get_implementation(),
   *     other.get_service(), other.get_implementation()); @endcode
   *
   * @note Available only for services that support movability,
   */
  basic_io_object& operator=(basic_io_object&& other);

  /// Perform a converting move-construction of a basic_io_object.
  template <typename IoObjectService1>
  basic_io_object(IoObjectService1& other_service,
      typename IoObjectService1::implementation_type& other_implementation);
#endif // defined(GENERATING_DOCUMENTATION)

  /// Protected destructor to prevent deletion through this type.
  /**
   * Performs:
   * @code get_service().destroy(get_implementation()); @endcode
   */
  ~basic_io_object()
  {
    service_.destroy(implementation_);
  }

#if 1
  /// Get the service associated with the I/O object.
  service_type get_service()
  {
    return service_;
  }
  /// Get the service associated with the I/O object.
  const service_type get_service() const
  {
    return service_;
  }

  /// Get the underlying implementation of the I/O object.
  implementation_type& get_implementation()
  {
    return implementation_;
  }

  /// Get the underlying implementation of the I/O object.
  const implementation_type& get_implementation() const
  {
    return implementation_;
  }
#endif
private:
  basic_io_object(const basic_io_object&);
  basic_io_object& operator=(const basic_io_object&);

  // The service associated with the I/O object.
  service_type service_;

  /// The underlying implementation of the I/O object.
  implementation_type implementation_;
};

#if defined(NET_TS_HAS_MOVE)
// Specialisation for movable objects.
template <typename IoObjectService1, typename IoObjectService2>
class basic_io_object<IoObjectService1, IoObjectService2, true>
{
public:
  using service_type1 = IoObjectService1;
  using service_type2 = IoObjectService2;
  /// The type of the service that will be used to provide I/O operations.
  union service_type {
    IoObjectService1* svc1;
    IoObjectService2* svc2;
  };

  using implementation_type1 = typename IoObjectService1::implementation_type;
  using implementation_type2 = typename IoObjectService2::implementation_type;

  /// The underlying implementation type of I/O object.
  union implementation_type {
    implementation_type1 impl1;
    implementation_type2 impl2;
    implementation_type() {}
    ~implementation_type() {}
  };

  typedef std::experimental::net::io_context::executor_type executor_type;

  executor_type get_executor() NET_TS_NOEXCEPT
  {
    return service_->get_io_context().get_executor();
  }

protected:
  explicit basic_io_object(std::experimental::net::io_context& io_context)
    : meta_(io_context.get_meta())
  {
    if (meta_ == 0) {
      service_.svc1 = &std::experimental::net::use_service<IoObjectService1>(io_context);
      new ((void*)&implementation_.impl1) implementation_type1();
      service_.svc1->construct(implementation_.impl1);
    }
    else {
      service_.svc2 = &std::experimental::net::use_service<IoObjectService2>(io_context);
      new ((void*)&implementation_.impl2) implementation_type2();
      service_.svc2->construct(implementation_.impl2);
    }

     NET_TS_SVC_INVOKE_(construct);
  }

  basic_io_object(basic_io_object&& other)
    : service_(&other.get_service())
  {
    service_->move_construct(implementation_, other.implementation_);
  }

  template <typename IoObjectService1>
  basic_io_object(IoObjectService1& other_service,
      typename IoObjectService1::implementation_type& other_implementation)
    : service_(&std::experimental::net::use_service<IoObjectService>(
          other_service.get_io_context()))
  {
    service_->converting_move_construct(implementation_,
        other_service, other_implementation);
  }

  ~basic_io_object()
  {
    NET_TS_SVC_INVOKE_(destroy);
    if (meta_ == 0)
      implementation_.impl1.~implementation_type1();
    else
      implementation_.impl2.~implementation_type2();
  }

  basic_io_object& operator=(basic_io_object&& other)
  {
    service_->move_assign(implementation_,
        *other.service_, other.implementation_);
    service_ = other.service_;
    return *this;
  }
#if 1
  auto &get_service1() { return *service_.svc1; }
  auto &get_service2() { return *service_.svc2; }

  auto &get_service1() const { return *service_.svc1; }
  auto &get_service2() const { return *service_.svc2; }

  auto &get_implementation1() { return implementation_.impl1; }
  auto &get_implementation2() { return implementation_.impl2; }

  auto &get_implementation1() const { return implementation_.impl1; }
  auto &get_implementation2() const { return implementation_.impl2; }

  int get_meta() const { return meta_; }
#endif
private:
  basic_io_object(const basic_io_object&);
  void operator=(const basic_io_object&);

  const int meta_;

  service_type service_;
  implementation_type implementation_;
};
#endif // defined(NET_TS_HAS_MOVE)

} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // NET_TS_BASIC_IO_OBJECT_HPP
