//
// ip/basic_endpoint.hpp
// ~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_IP_BASIC_ENDPOINT_HPP
#define NET_TS_IP_BASIC_ENDPOINT_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>
#include <experimental/__net_ts/ip/address.hpp>
#include <experimental/__net_ts/ip/detail/endpoint.hpp>

#if !defined(NET_TS_NO_IOSTREAM)
# include <iosfwd>
#endif // !defined(NET_TS_NO_IOSTREAM)

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std {
namespace experimental {
namespace net {
inline namespace v1 {
namespace ip {

/// Describes an endpoint for a version-independent IP socket.
/**
 * The std::experimental::net::v1::ip::basic_endpoint class template describes an endpoint that
 * may be associated with a particular socket.
 *
 * @par Thread Safety
 * @e Distinct @e objects: Safe.@n
 * @e Shared @e objects: Unsafe.
 *
 * @par Concepts:
 * Endpoint.
 */
template <typename InternetProtocol>
class basic_endpoint
{
public:
  /// The protocol type associated with the endpoint.
  typedef InternetProtocol protocol_type;

  /// The type of the endpoint structure. This type is dependent on the
  /// underlying implementation of the socket layer.
#if defined(GENERATING_DOCUMENTATION)
  typedef implementation_defined data_type;
#else
  typedef std::experimental::net::v1::detail::socket_addr_type data_type;
#endif

  /// Default constructor.
  basic_endpoint()
    : impl_()
  {
  }

  /// Construct an endpoint using a port number, specified in the host's byte
  /// order. The IP address will be the any address (i.e. INADDR_ANY or
  /// in6addr_any). This constructor would typically be used for accepting new
  /// connections.
  /**
   * @par Examples
   * To initialise an IPv4 TCP endpoint for port 1234, use:
   * @code
   * std::experimental::net::ip::tcp::endpoint ep(std::experimental::net::ip::tcp::v4(), 1234);
   * @endcode
   *
   * To specify an IPv6 UDP endpoint for port 9876, use:
   * @code
   * std::experimental::net::ip::udp::endpoint ep(std::experimental::net::ip::udp::v6(), 9876);
   * @endcode
   */
  basic_endpoint(const InternetProtocol& internet_protocol,
      unsigned short port_num)
    : impl_(internet_protocol.family(), port_num)
  {
  }

  /// Construct an endpoint using a port number and an IP address. This
  /// constructor may be used for accepting connections on a specific interface
  /// or for making a connection to a remote endpoint.
  basic_endpoint(const std::experimental::net::v1::ip::address& addr, unsigned short port_num)
    : impl_(addr, port_num)
  {
  }

  /// Copy constructor.
  basic_endpoint(const basic_endpoint& other)
    : impl_(other.impl_)
  {
  }

#if defined(NET_TS_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)
  /// Move constructor.
  basic_endpoint(basic_endpoint&& other)
    : impl_(other.impl_)
  {
  }
#endif // defined(NET_TS_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)

  /// Assign from another endpoint.
  basic_endpoint& operator=(const basic_endpoint& other)
  {
    impl_ = other.impl_;
    return *this;
  }

#if defined(NET_TS_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)
  /// Move-assign from another endpoint.
  basic_endpoint& operator=(basic_endpoint&& other)
  {
    impl_ = other.impl_;
    return *this;
  }
#endif // defined(NET_TS_HAS_MOVE) || defined(GENERATING_DOCUMENTATION)

  /// The protocol associated with the endpoint.
  protocol_type protocol() const
  {
    if (impl_.is_v4())
      return InternetProtocol::v4();
    return InternetProtocol::v6();
  }

  /// Get the underlying endpoint in the native type.
  data_type* data()
  {
    return impl_.data();
  }

  /// Get the underlying endpoint in the native type.
  const data_type* data() const
  {
    return impl_.data();
  }

  /// Get the underlying size of the endpoint in the native type.
  std::size_t size() const
  {
    return impl_.size();
  }

  /// Set the underlying size of the endpoint in the native type.
  void resize(std::size_t new_size)
  {
    impl_.resize(new_size);
  }

  /// Get the capacity of the endpoint in the native type.
  std::size_t capacity() const
  {
    return impl_.capacity();
  }

  /// Get the port associated with the endpoint. The port number is always in
  /// the host's byte order.
  unsigned short port() const
  {
    return impl_.port();
  }

  /// Set the port associated with the endpoint. The port number is always in
  /// the host's byte order.
  void port(unsigned short port_num)
  {
    impl_.port(port_num);
  }

  /// Get the IP address associated with the endpoint.
  std::experimental::net::v1::ip::address address() const
  {
    return impl_.address();
  }

  /// Set the IP address associated with the endpoint.
  void address(const std::experimental::net::v1::ip::address& addr)
  {
    impl_.address(addr);
  }

  /// Compare two endpoints for equality.
  friend bool operator==(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return e1.impl_ == e2.impl_;
  }

  /// Compare two endpoints for inequality.
  friend bool operator!=(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return !(e1 == e2);
  }

  /// Compare endpoints for ordering.
  friend bool operator<(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return e1.impl_ < e2.impl_;
  }

  /// Compare endpoints for ordering.
  friend bool operator>(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return e2.impl_ < e1.impl_;
  }

  /// Compare endpoints for ordering.
  friend bool operator<=(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return !(e2 < e1);
  }

  /// Compare endpoints for ordering.
  friend bool operator>=(const basic_endpoint<InternetProtocol>& e1,
      const basic_endpoint<InternetProtocol>& e2)
  {
    return !(e1 < e2);
  }

private:
  // The underlying IP endpoint.
  std::experimental::net::v1::ip::detail::endpoint impl_;
};

#if !defined(NET_TS_NO_IOSTREAM)

/// Output an endpoint as a string.
/**
 * Used to output a human-readable string for a specified endpoint.
 *
 * @param os The output stream to which the string will be written.
 *
 * @param endpoint The endpoint to be written.
 *
 * @return The output stream.
 *
 * @relates std::experimental::net::v1::ip::basic_endpoint
 */
template <typename Elem, typename Traits, typename InternetProtocol>
std::basic_ostream<Elem, Traits>& operator<<(
    std::basic_ostream<Elem, Traits>& os,
    const basic_endpoint<InternetProtocol>& endpoint);

#endif // !defined(NET_TS_NO_IOSTREAM)

} // namespace ip
} // inline namespace v1
} // namespace net
} // namespace experimental
} // namespace std

#include <experimental/__net_ts/detail/pop_options.hpp>

#include <experimental/__net_ts/ip/impl/basic_endpoint.hpp>

#endif // NET_TS_IP_BASIC_ENDPOINT_HPP
