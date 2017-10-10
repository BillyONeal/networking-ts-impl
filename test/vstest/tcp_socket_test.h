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

std::atomic<int> client_count;

class stats
{
public:
  stats(int timeout)
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
class client_session
{
public:
  client_session(IoContext &ioc, size_t block_size, stats &s)
      : strand_(ioc.get_executor()), socket_(ioc), io_context_(ioc),
        block_size_(block_size), read_data_(new char[block_size]),
        read_data_length_(0), write_data_(new char[block_size]),
        unwritten_count_(0), bytes_written_(0), bytes_read_(0), stats_(s) {
    ++client_count;
    for (size_t i = 0; i < block_size_; ++i)
      write_data_[i] = static_cast<char>(i % 128);
  }

  ~client_session()
  {
    stats_.add(bytes_written_, bytes_read_);

    delete[] read_data_;
    delete[] write_data_;
  }

  void start(std::experimental::net::ip::tcp::endpoint ep)
  {
    std::experimental::net::async_connect(socket_, &ep, &ep + 1,
      std::experimental::net::bind_executor(strand_,
        [this](auto ec, auto) { handle_connect(ec); }));
  }

  void stop()
  {
    std::experimental::net::post(strand_, [this] { close_socket(); });
  }

private:
  void handle_connect(const std::error_code& err)
  {
    if (!err)
    {
      std::error_code set_option_err;
      std::experimental::net::ip::tcp::no_delay no_delay(true);
      socket_.set_option(no_delay, set_option_err);
      if (!set_option_err)
      {
        ++unwritten_count_;
        async_write(socket_, std::experimental::net::buffer(write_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            //make_custom_alloc_handler(write_allocator_,
            [this](auto ec, auto n) { handle_write(ec, n); }));
        socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(read_allocator_,
              [this](auto ec, auto n) { handle_read(ec, n); })));
      }
    }
  }

  void handle_read(const std::error_code& err, size_t length)
  {
    if (!err)
    {
      bytes_read_ += length;

      read_data_length_ = length;
      ++unwritten_count_;
      if (unwritten_count_ == 1)
      {
        std::swap(read_data_, write_data_);
        async_write(socket_, std::experimental::net::buffer(write_data_, read_data_length_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(write_allocator_,
              [this](auto ec, auto n) { handle_write(ec, n); })));
        socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(read_allocator_,
              [this](auto ec, auto n) { handle_read(ec, n); })));
      }
    }
  }

  void handle_write(const std::error_code& err, size_t length)
  {
    if (!err && length > 0)
    {
      bytes_written_ += length;

      --unwritten_count_;
      if (unwritten_count_ == 1)
      {
        std::swap(read_data_, write_data_);
        async_write(socket_, std::experimental::net::buffer(write_data_, read_data_length_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(write_allocator_,
              [this](auto ec, auto n) { handle_write(ec, n); })));
        socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(read_allocator_,
              [this](auto ec, auto n) { handle_read(ec, n); })));
      }
    }
  }

  void close_socket()
  {
    socket_.close();

    if (--client_count == 0)
      io_context_.stop();
  }

private:
  IoContext& io_context_;
  std::experimental::net::strand<typename IoContext::executor_type> strand_;
  std::experimental::net::ip::tcp::socket socket_;
  size_t block_size_;
  char* read_data_;
  size_t read_data_length_;
  char* write_data_;
  int unwritten_count_;
  size_t bytes_written_;
  size_t bytes_read_;
  stats& stats_;
  handler_allocator read_allocator_;
  handler_allocator write_allocator_;
};

template <typename IoContext>
class client
{
public:
  client(IoContext& ioc,
    const std::experimental::net::ip::tcp::endpoint endpoints,
    size_t block_size, size_t session_count, int timeout)
    : io_context_(ioc),
    stop_timer_(ioc),
    sessions_(),
    stats_(timeout)
  {
    stop_timer_.expires_after(std::chrono::seconds(timeout));
    stop_timer_.async_wait([this](auto) { handle_timeout(); });

    for (size_t i = 0; i < session_count; ++i)
    {
      auto *new_session =
          new client_session<IoContext>(io_context_, block_size, stats_);
      new_session->start(endpoints);
      sessions_.push_back(new_session);
    }
  }

  ~client()
  {
    while (!sessions_.empty())
    {
      delete sessions_.front();
      sessions_.pop_front();
    }

    stats_.print();
    exit(0);
  }

  void handle_timeout()
  {
    for (auto *session : sessions_)
      session->stop();
  }

private:
  IoContext& io_context_;
  std::experimental::net::system_timer stop_timer_;
  std::list<client_session<IoContext>*> sessions_;
  stats stats_;
};

template <typename IoContext>
class session
{
public:
  session(IoContext &ioc,
    std::experimental::net::ip::tcp::socket s, size_t block_size)
    : io_context_(ioc),
    strand_(ioc.get_executor()),
    socket_(std::move(s)),
    block_size_(block_size),
    read_data_(new char[block_size]),
    read_data_length_(0),
    write_data_(new char[block_size]),
    unsent_count_(0),
    op_count_(0)
  {
  }

  ~session()
  {
    delete[] read_data_;
    delete[] write_data_;
  }

  std::experimental::net::ip::tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    std::error_code set_option_err;
    std::experimental::net::ip::tcp::no_delay no_delay(true);
    socket_.set_option(no_delay, set_option_err);
    if (!set_option_err)
    {
      ++op_count_;
      socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
        std::experimental::net::bind_executor(strand_,
          make_custom_alloc_handler(read_allocator_,
            [this](auto ec, auto n) {session::handle_read(ec, n); })));
    }
    else
    {
      std::experimental::net::post(io_context_, [this] {destroy(this); });
    }
  }

  void handle_read(const std::error_code& err, size_t length)
  {
    --op_count_;

    if (!err)
    {
      read_data_length_ = length;
      ++unsent_count_;
      if (unsent_count_ == 1)
      {
        op_count_ += 2;
        std::swap(read_data_, write_data_);
        async_write(socket_, std::experimental::net::buffer(write_data_, read_data_length_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(write_allocator_,
              [this](auto ec, auto) { handle_write(ec); })));
        socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(read_allocator_,
              [this](auto ec, auto n) { handle_read(ec, n); })));
      }
    }

    if (op_count_ == 0)
      std::experimental::net::post(io_context_, [this] {destroy(this); });
  }

  void handle_write(const std::error_code& err)
  {
    --op_count_;

    if (!err)
    {
      --unsent_count_;
      if (unsent_count_ == 1)
      {
        op_count_ += 2;
        std::swap(read_data_, write_data_);
        async_write(socket_, std::experimental::net::buffer(write_data_, read_data_length_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(write_allocator_,
              [this](auto ec, auto) { handle_write(ec); })));
        socket_.async_read_some(std::experimental::net::buffer(read_data_, block_size_),
          std::experimental::net::bind_executor(strand_,
            make_custom_alloc_handler(read_allocator_,
              [this](auto ec, auto n) { handle_read(ec, n); })));
      }
    }

    if (op_count_ == 0)
      std::experimental::net::post(io_context_, [this] {destroy(this); });
  }

  static void destroy(session* s)
  {
    delete s;
  }

private:
  IoContext& io_context_;
  std::experimental::net::strand<typename IoContext::executor_type> strand_;
  std::experimental::net::ip::tcp::socket socket_;
  size_t block_size_;
  char* read_data_;
  size_t read_data_length_;
  char* write_data_;
  int unsent_count_;
  int op_count_;
  handler_allocator read_allocator_;
  handler_allocator write_allocator_;
};

template <typename IoContext, typename TpContext>
class server
{
  TpContext& tp;
public:
  server(IoContext &ioc, TpContext& tp,
         const std::experimental::net::ip::tcp::endpoint &endpoint,
         size_t block_size)
      : tp(tp), io_context_(ioc), acceptor_(tp), block_size_(block_size) {
    using namespace std::experimental::net;
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(ip::tcp::acceptor::reuse_address(1));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    start_accept();
  }

  void start_accept()
  {
    acceptor_.async_accept(io_context_,
      [this](auto ec, auto s) { handle_accept(ec, std::move(s)); });
  }

  void handle_accept(std::error_code err,
                     std::experimental::net::ip::tcp::socket s) {
    if (!err) {
      auto *new_session =
          new session<IoContext>(io_context_, std::move(s), block_size_);
      new_session->start();
    }
    start_accept();
  }

private:
  IoContext& io_context_;
  std::experimental::net::ip::tcp::acceptor acceptor_;
  size_t block_size_;
};

// 7MBs vs 30MBps
template <typename IoContext, typename F>
void tcp_socket_test(const char* label, F run)
{
  using namespace std::experimental::net;
  char const *args[7] = {label, "127.0.0.1", "8888", "8", "128", "14", "1" };
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
    //tp_context tpc;
#define tp ioc
    //IoContext& tp = ioc;

    //ip::tcp::resolver r(ioc);
    //ip::tcp::resolver::results_type endpoints = r.resolve(host, port);

    auto ep = ip::tcp::endpoint(address, atoi(port));

    server<IoContext, decltype(tp)> s(
        ioc, tp, ep, block_size);
    {
      client<IoContext> c(ioc, ep, block_size, session_count, timeout);
      run(ioc);
    }

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
