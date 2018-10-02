#pragma once


#if 1
//
// vstest.cpp
// ~~~~~~~~~~
//
// Slightly modified version of
// https://github.com/chriskohlhoff/asio/blob/master/asio/src/tests/performance/client.cpp
// that has the following copyright:
//
// Copyright (c) 2003-2017 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "handler_allocator.hpp"
#include <atomic>
#include <experimental/internet>
#include <experimental/net>
#include <experimental/timer>
#include <iostream>
#include <mutex>
#include <string>
#include <memory>
#include <vector>

//#include "tp_context.h"
//#include "wintp_socket.h"

#ifdef COUNT_ALLOCS
std::atomic<int> allocs;

void* operator new(size_t n) {
  allocs.fetch_add(1, std::memory_order::memory_order_relaxed);
  return malloc(n);
}
void operator delete(void* p) noexcept {
  free(p);
}
#endif

std::atomic<int> udp_client_count;

class udp_stats
{
public:
  udp_stats(int timeout)
    : mutex_(),
    timeout_(timeout),
    total_bytes_written_(0),
    total_bytes_read_(0)
  {
  }

  void add(size_t bytes_written, size_t bytes_read)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    total_bytes_written_ += bytes_written;
    total_bytes_read_ += bytes_read;
  }

  void print()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << scale(total_bytes_written_) << " Mbytes written per second\n";
    std::cout << scale(total_bytes_read_) << " Mbytes read per second\n";
  }

  double scale(size_t bytes) const
  {
    return static_cast<double>(bytes) / timeout_ / 1024 / 1024;
  }

private:
  std::mutex mutex_;
  const int timeout_;
  size_t total_bytes_written_;
  size_t total_bytes_read_;
};

template <typename IoContext>
class udp_client_server_base
{
public:
  udp_client_server_base(
      IoContext &ioc,
      const std::experimental::net::ip::udp::endpoint &local_endpoint,
      const std::experimental::net::ip::udp::endpoint &remote_endpoint,
      size_t block_size)
      : io_context_(ioc), socket_(ioc, local_endpoint),
        remote_endpoint_(remote_endpoint), block_size_(block_size),
        read_data_(block_size) {}

  udp_client_server_base(IoContext &ioc,
    const std::experimental::net::ip::udp::endpoint &endpoint,
    size_t block_size)
    : io_context_(ioc), socket_(ioc, endpoint), block_size_(block_size),
    read_data_(block_size) {
  }

  void start_receive()
  {
    socket_.async_receive_from(
      std::experimental::net::buffer(read_data_, block_size_),
      remote_endpoint_,
      make_custom_alloc_handler(
        read_allocator_, [this](auto ec, auto n) { handle_read(ec, n); }));
  }

  void start_write() {
    socket_.async_send_to(
      std::experimental::net::buffer(read_data_, block_size_),
      remote_endpoint_,
      make_custom_alloc_handler(
        write_allocator_, [this](auto ec, auto n) { handle_write(ec, n); }));
  }

  void handle_read(std::error_code err, size_t n) {
    if (!err) {
      bytes_read_ += n;
      start_write();
    }
    else {
      printf("read got error %d\n", err.value());
    }
  }

  void handle_write(std::error_code err, size_t n) {
    if (!err) {
      bytes_written_ += n;
      start_receive();
    }
    else {
      printf("write got error %d\n", err.value());
    }
  }

protected:
  size_t bytes_written_ = 0;
  size_t bytes_read_ = 0;

  handler_allocator read_allocator_;
  handler_allocator write_allocator_;

  std::vector<char> read_data_;
  IoContext& io_context_;
  std::experimental::net::ip::udp::endpoint remote_endpoint_;
  std::experimental::net::ip::udp::socket socket_;
  size_t block_size_;
};

template <typename IoContext>
class udp_client : udp_client_server_base<IoContext>
{
public:
  udp_client(IoContext &ioc,
             const std::experimental::net::ip::udp::endpoint &endpoint,
             size_t block_size, int timeout)
      : udp_client_server_base<IoContext>(
            ioc, std::experimental::net::ip::udp::endpoint(), endpoint,
            block_size), stop_timer_(ioc) {
    stop_timer_.expires_after(std::chrono::seconds(timeout));
    stop_timer_.async_wait([this, timeout](auto) {
      udp_stats stats(timeout);
      stats.add(this->bytes_written_, this->bytes_read_);
      stats.print();
      printf("stopping...\n");
      this->io_context_.stop();
    });

    this->start_write();
  }
private:
  std::experimental::net::system_timer stop_timer_;
};

template <typename IoContext>
class udp_server : udp_client_server_base<IoContext>
{
public:
  udp_server(IoContext &ioc,
             const std::experimental::net::ip::udp::endpoint &endpoint,
             size_t block_size)
      : udp_client_server_base<IoContext>(
            ioc, endpoint, std::experimental::net::ip::udp::endpoint(),
            block_size) {
    this->start_receive();
  }
};

// 7MBs vs 30MBps
template <typename IoContext, typename F>
void udp_socket_test(const char* label, F run)
{
  using namespace std::experimental::net;
  char const *args[7] = { label, "127.0.0.1", "8888", "8", "128", "14", "1" };
  try {
    using namespace std; // For atoi.
    const char *host = args[1];
    const char *port = args[2];
    int thread_count = atoi(args[3]);
    size_t block_size = atoi(args[4]);
    size_t session_count = atoi(args[5]);
    int timeout = atoi(args[6]);
    ip::address address = ip::make_address(host);

    IoContext ioc;
    auto ep = ip::udp::endpoint(address, atoi(port));

    printf("%s: testing udp client/server\n", label);

    udp_server<IoContext> s(ioc, ep, block_size);
    udp_client<IoContext> c(ioc, ep, block_size, timeout);

    run(ioc);

#ifdef COUNT_ALLOCS
    std::cout << "allocs: " << allocs.load() << "\n";
#endif
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }
}
#endif
