//
// detail/wintp_socket_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Microsoft
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NET_TS_DETAIL_WINTP_SOCKET_SERVICE_HPP
#define NET_TS_DETAIL_WINTP_SOCKET_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <experimental/__net_ts/detail/config.hpp>

#if defined(NET_TS_HAS_IOCP)

#include <cstring>
#include <experimental/__net_ts/error.hpp>
#include <experimental/__net_ts/io_context.hpp>
#include <experimental/__net_ts/socket_base.hpp>
#include <experimental/__net_ts/detail/bind_handler.hpp>
#include <experimental/__net_ts/detail/buffer_sequence_adapter.hpp>
#include <experimental/__net_ts/detail/fenced_block.hpp>
#include <experimental/__net_ts/detail/handler_alloc_helpers.hpp>
#include <experimental/__net_ts/detail/handler_invoke_helpers.hpp>
#include <experimental/__net_ts/detail/memory.hpp>
#include <experimental/__net_ts/detail/mutex.hpp>
#include <experimental/__net_ts/detail/operation.hpp>
#include <experimental/__net_ts/detail/reactor_op.hpp>
#include <experimental/__net_ts/detail/select_reactor.hpp>
#include <experimental/__net_ts/detail/socket_holder.hpp>
#include <experimental/__net_ts/detail/socket_ops.hpp>
#include <experimental/__net_ts/detail/socket_types.hpp>
#include <experimental/tp_context>
#include <experimental/__net_ts/detail/win_iocp_io_context.hpp>
#include <experimental/__net_ts/detail/win_iocp_null_buffers_op.hpp>
#include <experimental/__net_ts/detail/win_iocp_socket_accept_op.hpp>
#include <experimental/__net_ts/detail/win_iocp_socket_connect_op.hpp>
#include <experimental/__net_ts/detail/win_iocp_socket_recvfrom_op.hpp>
#include <experimental/__net_ts/detail/win_iocp_socket_send_op.hpp>
#include <experimental/__net_ts/detail/win_iocp_socket_service_base.hpp>
#include <experimental/__net_ts/detail/simple_intrusive_list.hpp>
#include <experimental/__net_ts/detail/cancellable_object_owner.hpp>
#include <experimental/__net_ts/detail/wintp_scheduler.hpp>

#include <experimental/__net_ts/detail/push_options.hpp>

namespace std::experimental::net {
inline namespace v1 {
namespace detail {

  template <typename Protocol>
  class wintp_socket_service :
    public service_base<wintp_socket_service<Protocol>>,
    public cancellable_service
  {
  public:
    typedef BOOL(PASCAL *connect_ex_fn)(SOCKET,
      const socket_addr_type*, int, void*, DWORD, DWORD*, OVERLAPPED*);

    // The protocol type.
    typedef Protocol protocol_type;

    // The endpoint type.
    typedef typename Protocol::endpoint endpoint_type;

    using socket_service = wintp_socket_service<Protocol>;

    using endpoint_type = typename Protocol::endpoint;
    using protocol_type = Protocol;

    struct socket_impl : cancellable_object_base {
      PTP_IO io = nullptr;
      socket_type socket_ = invalid_socket;
      socket_ops::state_type state_;
      socket_ops::shared_cancel_token_type cancel_token_;

      // The protocol associated with the socket.
      protocol_type protocol_ = endpoint_type().protocol();

      // Whether we have a cached remote endpoint.
      bool have_remote_endpoint_ = false;

      // A cached remote endpoint.
      endpoint_type remote_endpoint_;

      wintp_socket_service& service;

      void initiate_cancel() {
        if (socket_ != invalid_socket) {
          if (!CancelIoEx(reinterpret_cast<HANDLE>(socket_), nullptr)) {
            auto error = GetLastError();
            if (error != ERROR_NOT_FOUND)
              printf("CancelIoEx failed => %d\n", GetLastError());
          }
        }
      }

      void cancel() {
        if (try_set_cancel_pending("socket_impl")) {
          initiate_cancel();
          service.owner.remove_child(this);
        }
      }

      socket_impl(wintp_socket_service& service) : service(service) {}

      ~socket_impl() {
        if (io) {
          WaitForThreadpoolIoCallbacks(io, false);
          CloseThreadpoolIo(io);
        }

        std::error_code ec;
        if (socket_ != invalid_socket)
          socket_ops::close(socket_, state_, /*destr=*/true, ec);
      }
    };

    // The native type of a socket.
    using native_handle_type = typename detail::win_iocp_socket_service<Protocol>::native_handle_type;
    using implementation_type = socket_impl*;

    // FIXME: use object_pool
    void destroy(implementation_type &t) {
      if (t) {
        t->cancel();
        t->drop_ref("Handle");
        t = nullptr;
      }
    }

    void construct(implementation_type &t) {
      auto p = make_unique<socket_impl>(*this);
      owner.add_child(p.get());
      t = p.release();
    }

    void initiate_cancel() override { owner.cancel_all("socket_service"); }

    void move_construct(implementation_type &x, implementation_type &y)
    {
      x = std::move(y);
      y = nullptr;
    }

    connect_ex_fn get_connect_ex(implementation_type &impl, int type)
    {
      if (type != NET_TS_OS_DEF(SOCK_STREAM)
        && type != NET_TS_OS_DEF(SOCK_SEQPACKET))
        throw std::logic_error("ConnextEx is required");

      void* ptr = InterlockedCompareExchangePointer(&connect_ex_, 0, 0);
      if (!ptr)
      {
        GUID guid = { 0x25a207b9, 0xddf3, 0x4660,
        { 0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e } };

        DWORD bytes = 0;
        if (::WSAIoctl(impl->socket_, SIO_GET_EXTENSION_FUNCTION_POINTER,
          &guid, sizeof(guid), &ptr, sizeof(ptr), &bytes, 0, 0) != 0)
        {
          // Set connect_ex_ to a special value to indicate that ConnectEx is
          // unavailable. That way we won't bother trying to look it up again.
          ptr = this;
          throw std::logic_error("ConnextEx is required");
        }

        InterlockedExchangePointer(&connect_ex_, ptr);
      }
      return reinterpret_cast<connect_ex_fn>(ptr == this ? 0 : ptr);
    }

    std::error_code do_assign(implementation_type &impl, int type,
                              socket_type native_socket, std::error_code &ec)
    {
      if (is_open(impl))
      {
        ec = std::experimental::net::error::already_open;
        return ec;
      }

      HANDLE sock_as_handle = reinterpret_cast<HANDLE>(native_socket);
      if (register_handle(sock_as_handle, impl, ec))
        return ec;

      impl->socket_ = native_socket;
      switch (type)
      {
      case SOCK_STREAM: impl->state_ = socket_ops::stream_oriented; break;
      case SOCK_DGRAM: impl->state_ = socket_ops::datagram_oriented; break;
      default: impl->state_ = 0; break;
      }
      impl->cancel_token_.reset(static_cast<void*>(0), socket_ops::noop_deleter());
      ec = std::error_code();
      return ec;
    }

    std::error_code listen(implementation_type &impl, int backlog,
                           std::error_code &ec) {
      socket_ops::listen(impl->socket_, backlog, ec);
      return ec;
      }

      // Determine whether the socket is open.
      bool is_open(const implementation_type &impl) const {
        return impl->socket_ != invalid_socket;
      }

      std::error_code close(implementation_type & impl, std::error_code & ec) {
        socket_ops::close(impl->socket_, impl->state_, false, ec);

        impl->socket_ = invalid_socket;
        impl->state_ = 0;
        impl->cancel_token_.reset();

        return ec;
      }

      void restart_accept_op(socket_type s, socket_holder & new_socket,
                             int family, int type, int protocol,
                             void *output_buffer, DWORD address_length,
                             operation *op) {
        throw std::logic_error("restart_accept_op: not implemented yet");
      }

      struct tpio_service {
        void work_started() { scheduler.work_started(); }
        void on_completion(win_iocp_operation *op, DWORD last_error = 0,
                           DWORD bytes_transferred = 0) {

          // Store results in the OVERLAPPED structure.
          op->Internal = reinterpret_cast<ULONG_PTR>(&std::system_category());
          op->Offset = last_error;
          op->OffsetHigh = bytes_transferred;

          scheduler.reserved_post(op);
        }
        void on_completion(win_iocp_operation *op, std::error_code ec,
                           DWORD bytes_transferred = 0) {

          // Store results in the OVERLAPPED structure.
          op->Internal = reinterpret_cast<ULONG_PTR>(&ec.category());
          op->Offset = ec.value();
          op->OffsetHigh = bytes_transferred;

          scheduler.reserved_post(op);
        }
        void on_pending(win_iocp_operation *op) {}

        void post_immediate_completion(win_iocp_operation* op, bool)
        {
          work_started();
          on_completion(op);
        }

        wintp_scheduler &scheduler;
      };
      tpio_service iocp_service_;

      std::error_code register_handle(HANDLE handle, implementation_type & impl,
                                      std::error_code & ec) {
        impl->io = CreateThreadpoolIo(
            handle,
            [](auto, void *ctx, void *over, auto IoResult, auto nBytes, auto) {
              auto *o = static_cast<OVERLAPPED *>(over);
              auto op = static_cast<win_iocp_operation *>(o);
              auto *impl = static_cast<implementation_type>(ctx);
              auto &service = impl->service;
#if 0
              // Store results in the OVERLAPPED structure.
              op->Internal = reinterpret_cast<ULONG_PTR>(&std::system_category());
              op->Offset = IoResult;
              op->OffsetHigh = nBytes;
#endif
              std::error_code result_ec{(int)IoResult, std::system_category()};

              try { op->complete(&service, result_ec, nBytes); }
              catch (...) {}
              impl->drop_ref("io done");
              service.iocp_service_.scheduler.work_finished();
            },
            impl, nullptr);
        // FIXME: Capture windows error
        if (!impl->io)
          ec = impl->io ? std::error_code{} : error::no_memory;
        return ec;
      }

      std::error_code do_open(implementation_type & impl, int family, int type,
                              int protocol, std::error_code &ec) {
        if (is_open(impl)) {
          ec = std::experimental::net::error::already_open;
          return ec;
        }

        socket_holder sock(socket_ops::socket(family, type, protocol, ec));
        if (sock.get() == invalid_socket)
          return ec;

        HANDLE sock_as_handle = reinterpret_cast<HANDLE>(sock.get());

        if (register_handle(sock_as_handle, impl, ec))
          return ec;

        impl->socket_ = sock.release();
        switch (type) {
        case SOCK_STREAM:
          impl->state_ = socket_ops::stream_oriented;
          break;
        case SOCK_DGRAM:
          impl->state_ = socket_ops::datagram_oriented;
          break;
        default:
          impl->state_ = 0;
          break;
        }
        ec = std::error_code();
        return ec;
      }

      wintp_socket_service(io_context & io_context)
          : service_base<wintp_socket_service<Protocol>>(io_context),
            iocp_service_{dynamic_cast<tp_context &>(io_context).scheduler()}
      {
        iocp_service_.scheduler.add_child(this);
      }

      void start_accept_op(implementation_type & impl, bool peer_is_open,
                           socket_holder &new_socket, int family, int type,
                           int protocol, void *output_buffer,
                           DWORD address_length, operation *op) {
        iocp_service_.work_started();

        if (!is_open(impl))
          iocp_service_.on_completion(
              op, std::experimental::net::error::bad_descriptor);
        else if (peer_is_open)
          iocp_service_.on_completion(
              op, std::experimental::net::error::already_open);
        else {
          std::error_code ec;
          new_socket.reset(socket_ops::socket(family, type, protocol, ec));
          if (new_socket.get() == invalid_socket)
            iocp_service_.on_completion(op, ec);
          else {
            DWORD bytes_read = 0;
            if (!StartThreadpoolIo(impl, op))
              return;

            BOOL result = ::AcceptEx(impl->socket_, new_socket.get(), output_buffer, 0,
                           address_length, address_length, &bytes_read, op);
            DWORD last_error = ::WSAGetLastError();
            if (!result && last_error != WSA_IO_PENDING) {
              CancelThreadpoolIo(impl);
              iocp_service_.on_completion(op, last_error);
            } else
              iocp_service_.on_pending(op);
          }
        }
      }

      // FIXME: Refactor into base that does not depend on the protocol.

      std::error_code bind(implementation_type & impl,
                           const endpoint_type &endpoint, std::error_code &ec) {
        socket_ops::bind(impl->socket_, endpoint.data(), endpoint.size(), ec);
        return ec;
      }

      std::error_code open(implementation_type & impl,
                           const protocol_type &protocol, std::error_code &ec) {
        if (!do_open(impl, protocol.family(), protocol.type(),
                     protocol.protocol(), ec)) {
          impl->protocol_ = protocol;
          impl->have_remote_endpoint_ = false;
          impl->remote_endpoint_ = endpoint_type();
        }
        return ec;
      }

      void start_send_op(
        implementation_type& impl,
        WSABUF* buffers, std::size_t buffer_count,
        socket_base::message_flags flags, bool noop, operation* op)
      {
        iocp_service_.work_started();

        if (noop)
          iocp_service_.on_completion(op);
        else if (!is_open(impl))
          iocp_service_.on_completion(op, std::experimental::net::error::bad_descriptor);
        else
        {
          DWORD bytes_transferred = 0;
          if (!StartThreadpoolIo(impl, op))
            return;

          int result = ::WSASend(impl->socket_, buffers,
            static_cast<DWORD>(buffer_count), &bytes_transferred, flags, op, 0);
          DWORD last_error = ::WSAGetLastError();
          if (last_error == ERROR_PORT_UNREACHABLE)
            last_error = WSAECONNREFUSED;
          if (result != 0 && last_error != WSA_IO_PENDING)
          {
            CancelThreadpoolIo(impl);
            iocp_service_.on_completion(op, last_error, bytes_transferred);
          }
          else
            iocp_service_.on_pending(op);
        }
      }

      void start_send_to_op(implementation_type & impl, WSABUF * buffers,
                            std::size_t buffer_count,
                            const socket_addr_type *addr, int addrlen,
                            socket_base::message_flags flags, operation *op) {
        iocp_service_.work_started();

        if (!is_open(impl))
          iocp_service_.on_completion(
              op, std::experimental::net::error::bad_descriptor);
        else {
          DWORD bytes_transferred = 0;
          if (!StartThreadpoolIo(impl, op))
            return;

          int result = ::WSASendTo(
              impl->socket_, buffers, static_cast<DWORD>(buffer_count),
              &bytes_transferred, flags, addr, addrlen, op, 0);
          DWORD last_error = ::WSAGetLastError();
          if (last_error == ERROR_PORT_UNREACHABLE)
            last_error = WSAECONNREFUSED;
          if (result != 0 && last_error != WSA_IO_PENDING) {
            CancelThreadpoolIo(impl);
            iocp_service_.on_completion(op, last_error, bytes_transferred);
          } else
            iocp_service_.on_pending(op);
        }
      }

      void start_connect_op(implementation_type &impl, int family, int type,
        const socket_addr_type *addr, std::size_t addrlen,
        win_iocp_socket_connect_op_base *op) {
        // If ConnectEx is available, use that.
        if (family == NET_TS_OS_DEF(AF_INET)
          || family == NET_TS_OS_DEF(AF_INET6))
        {
          if (connect_ex_fn connect_ex = get_connect_ex(impl, type))
          {
            union address_union
            {
              socket_addr_type base;
              sockaddr_in4_type v4;
              sockaddr_in6_type v6;
            } a;

            using namespace std; // For memset.
            memset(&a, 0, sizeof(a));
            a.base.sa_family = family;

            socket_ops::bind(impl->socket_, &a.base,
              family == NET_TS_OS_DEF(AF_INET)
              ? sizeof(a.v4) : sizeof(a.v6), op->ec_);
            if (op->ec_ && op->ec_ != std::experimental::net::error::invalid_argument)
            {
              iocp_service_.post_immediate_completion(op, false);
              return;
            }

            op->connect_ex_ = true;
            iocp_service_.work_started();

            if (!StartThreadpoolIo(impl, op))
              return;

            BOOL result = connect_ex(impl->socket_, addr,
                                     static_cast<int>(addrlen), 0, 0, 0, op);
            DWORD last_error = ::WSAGetLastError();
            if (!result && last_error != WSA_IO_PENDING)
            {
              CancelThreadpoolIo(impl);
              iocp_service_.on_completion(op, last_error);
            }
            else
              iocp_service_.on_pending(op);
            return;
          }
        }
        throw std::logic_error("only connectEx supported");
      }

      void start_receive_from_op(
          implementation_type & impl, WSABUF * buffers,
          std::size_t buffer_count, socket_addr_type * addr,
          socket_base::message_flags flags, int *addrlen, operation *op) {
        iocp_service_.work_started();

        if (!is_open(impl))
          iocp_service_.on_completion(
              op, std::experimental::net::error::bad_descriptor);
        else {
          DWORD bytes_transferred = 0;
          DWORD recv_flags = flags;
          if (!StartThreadpoolIo(impl, op))
            return;
          int result = ::WSARecvFrom(
              impl->socket_, buffers, static_cast<DWORD>(buffer_count),
              &bytes_transferred, &recv_flags, addr, addrlen, op, 0);
          DWORD last_error = ::WSAGetLastError();
          if (last_error == ERROR_PORT_UNREACHABLE)
            last_error = WSAECONNREFUSED;
          if (result != 0 && last_error != WSA_IO_PENDING) {
            CancelThreadpoolIo(impl);
            iocp_service_.on_completion(op, last_error, bytes_transferred);
          } else
            iocp_service_.on_pending(op);
        }
      }

      bool StartThreadpoolIo(implementation_type& impl, operation* op) {
        if (!impl->try_increment_local_io()) {
          iocp_service_.on_completion(op, ERROR_OPERATION_ABORTED);
          return false;
        }

        ::StartThreadpoolIo(impl->io);
        return true;
      }

      void CancelThreadpoolIo(implementation_type& impl) {
        ::CancelThreadpoolIo(impl->io);
        impl->drop_ref("io failed");
      }

      void start_receive_op(
        implementation_type& impl,
        WSABUF* buffers, std::size_t buffer_count,
        socket_base::message_flags flags, bool noop, operation* op)
      {
        iocp_service_.work_started();

        if (noop)
          iocp_service_.on_completion(op);
        else if (!is_open(impl))
          iocp_service_.on_completion(op, std::experimental::net::error::bad_descriptor);
        else
        {
          DWORD bytes_transferred = 0;
          DWORD recv_flags = flags;
          if (!StartThreadpoolIo(impl, op))
            return;
          int result = ::WSARecv(impl->socket_, buffers,
                                 static_cast<DWORD>(buffer_count),
                                 &bytes_transferred, &recv_flags, op, 0);
          DWORD last_error = ::WSAGetLastError();
          if (last_error == ERROR_NETNAME_DELETED)
            last_error = WSAECONNRESET;
          else if (last_error == ERROR_PORT_UNREACHABLE)
            last_error = WSAECONNREFUSED;
          if (result != 0 && last_error != WSA_IO_PENDING) {
            CancelThreadpoolIo(impl);
            iocp_service_.on_completion(op, last_error, bytes_transferred);
          }
          else
            iocp_service_.on_pending(op);
        }
      }

      template <typename MutableBufferSequence, typename Handler>
      void async_receive_from(
          implementation_type & impl, const MutableBufferSequence &buffers,
          endpoint_type &sender_endp, socket_base::message_flags flags,
          Handler &handler) {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_recvfrom_op<MutableBufferSequence,
                                            endpoint_type, Handler>
            op;
        typename op::ptr p = {
            std::experimental::net::detail::addressof(handler),
            op::ptr::allocate(handler), 0};
        p.p = new (p.v) op(sender_endp, impl->cancel_token_, buffers, handler);

        NET_TS_HANDLER_CREATION((io_context_, *p.p, "socket", &impl,
                                 impl.socket_, "async_receive_from"));

        buffer_sequence_adapter<std::experimental::net::mutable_buffer,
                                MutableBufferSequence>
            bufs(buffers);

        start_receive_from_op(impl, bufs.buffers(), bufs.count(),
                              sender_endp.data(), flags, &p.p->endpoint_size(),
                              p.p);
        p.v = p.p = 0;
      }

      template <typename ConstBufferSequence, typename Handler>
      void async_send(implementation_type& impl,
        const ConstBufferSequence& buffers,
        socket_base::message_flags flags, Handler& handler)
      {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_send_op<ConstBufferSequence, Handler> op;
        typename op::ptr p = { std::experimental::net::detail::addressof(handler),
          op::ptr::allocate(handler), 0 };
        p.p = new (p.v) op(impl->cancel_token_, buffers, handler);

        NET_TS_HANDLER_CREATION((io_context_, *p.p, "socket",
          &impl, impl.socket_, "async_send"));

        buffer_sequence_adapter<std::experimental::net::const_buffer,
          ConstBufferSequence> bufs(buffers);

        start_send_op(impl, bufs.buffers(), bufs.count(), flags,
          (impl->state_ & socket_ops::stream_oriented) != 0 && bufs.all_empty(),
          p.p);
        p.v = p.p = 0;
      }

      // Start an asynchronous send. The data being sent must be valid for the
      // lifetime of the asynchronous operation.
      template <typename ConstBufferSequence, typename Handler>
      void async_send_to(implementation_type & impl,
                         const ConstBufferSequence &buffers,
                         const endpoint_type &destination,
                         socket_base::message_flags flags, Handler &handler) {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_send_op<ConstBufferSequence, Handler> op;
        typename op::ptr p = {
            std::experimental::net::detail::addressof(handler),
            op::ptr::allocate(handler), 0};
        p.p = new (p.v) op(impl->cancel_token_, buffers, handler);

        NET_TS_HANDLER_CREATION((io_context_, *p.p, "socket", &impl,
                                 impl.socket_, "async_send_to"));

        buffer_sequence_adapter<std::experimental::net::const_buffer,
                                ConstBufferSequence>
            bufs(buffers);

        start_send_to_op(impl, bufs.buffers(), bufs.count(), destination.data(),
                         static_cast<int>(destination.size()), flags, p.p);
        p.v = p.p = 0;
      }

      template <typename Handler>
      void async_accept(implementation_type & impl,
                        io_context * peer_io_context,
                        endpoint_type * peer_endpoint, Handler & handler) {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_move_accept_op<protocol_type, Handler, socket_service>
            op;
        typename op::ptr p = {
            std::experimental::net::detail::addressof(handler),
            op::ptr::allocate(handler), 0};
        bool enable_connection_aborted =
            (impl->state_ & socket_ops::enable_connection_aborted) != 0;
        p.p = new (p.v) op(*this, impl->socket_, impl->protocol_,
                           peer_io_context ? *peer_io_context : this->get_io_context(),
                           peer_endpoint, enable_connection_aborted, handler);

        NET_TS_HANDLER_CREATION(
            (io_context_, *p.p, "socket", &impl, impl.socket_, "async_accept"));

        start_accept_op(impl, false, p.p->new_socket(),
                        impl->protocol_.family(), impl->protocol_.type(),
                        impl->protocol_.protocol(), p.p->output_buffer(),
                        p.p->address_length(), p.p);
        p.v = p.p = 0;
      }

      // Start an asynchronous connect.
      template <typename Handler>
      void async_connect(implementation_type& impl,
        const endpoint_type& peer_endpoint, Handler& handler)
      {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_connect_op<Handler> op;
        typename op::ptr p = { std::experimental::net::detail::addressof(handler),
          op::ptr::allocate(handler), 0 };
        p.p = new (p.v) op(impl->socket_, handler);

        NET_TS_HANDLER_CREATION((io_context_, *p.p, "socket",
          &impl, impl.socket_, "async_connect"));

        start_connect_op(impl, impl->protocol_.family(), impl->protocol_.type(),
          peer_endpoint.data(), static_cast<int>(peer_endpoint.size()), p.p);
        p.v = p.p = 0;
      }

      template <typename Option>
      std::error_code set_option(implementation_type & impl,
                                 const Option &option, std::error_code &ec) {
        socket_ops::setsockopt(
            impl->socket_, impl->state_, option.level(impl->protocol_),
            option.name(impl->protocol_), option.data(impl->protocol_),
            option.size(impl->protocol_), ec);
        return ec;
      }

      template <typename MutableBufferSequence, typename Handler>
      void async_receive(implementation_type& impl,
        const MutableBufferSequence& buffers,
        socket_base::message_flags flags, Handler& handler)
      {
        // Allocate and construct an operation to wrap the handler.
        typedef win_iocp_socket_recv_op<MutableBufferSequence, Handler> op;
        typename op::ptr p = { std::experimental::net::detail::addressof(handler),
          op::ptr::allocate(handler), 0 };
        p.p = new (p.v) op(impl->state_, impl->cancel_token_, buffers, handler);

        NET_TS_HANDLER_CREATION((io_context_, *p.p, "socket",
          &impl, impl.socket_, "async_receive"));

        buffer_sequence_adapter<std::experimental::net::mutable_buffer,
          MutableBufferSequence> bufs(buffers);

        start_receive_op(impl, bufs.buffers(), bufs.count(), flags,
          (impl->state_ & socket_ops::stream_oriented) != 0 && bufs.all_empty(),
          p.p);
        p.v = p.p = 0;
      }

      // Assign a native socket to a socket implementation.
      std::error_code assign(implementation_type& impl,
        const protocol_type& protocol, const native_handle_type& native_socket,
        std::error_code& ec)
      {
        if (!do_assign(impl, protocol.type(), native_socket, ec))
        {
          impl->protocol_ = protocol;
          impl->have_remote_endpoint_ = native_socket.have_remote_endpoint();
          impl->remote_endpoint_ = native_socket.remote_endpoint();
        }
        return ec;
      }

      native_handle_type native_handle(implementation_type& t) {
        if (t->have_remote_endpoint_)
          return native_handle_type(t->socket_, t->remote_endpoint_);
        return native_handle_type(t->socket_);
      }

    private:
      // Pointer to ConnectEx implementation.
      void* connect_ex_ = nullptr;
      cancellable_object_owner<socket_impl> owner;
  };

} // namespace detail
} // namespace v1
} // namespace std::experimental::net

#include <experimental/__net_ts/detail/pop_options.hpp>

#endif // defined(NET_TS_HAS_IOCP)

#endif // NET_TS_DETAIL_WIN_IOCP_SOCKET_SERVICE_HPP
