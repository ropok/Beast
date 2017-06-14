//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLES_WEBSOCKET_ASYNC_ECHO_SERVER_HPP
#define BEAST_EXAMPLES_WEBSOCKET_ASYNC_ECHO_SERVER_HPP

#include "server.hpp"

#include <beast/core/multi_buffer.hpp>
#include <beast/websocket/stream.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

namespace server {

template<class = void>
class ws_async_echo_port_impl
{
    using on_new_stream_cb = std::function<
        void(beast::websocket::stream<socket_type>&)>;

    std::ostream& log_;
    on_new_stream_cb cb_;

    class connection;

public:
    ws_async_echo_port_impl(
        std::ostream& log, on_new_stream_cb cb);

    void
    on_accept(std::size_t id,
        socket_type&& sock, endpoint_type ep);
};

//------------------------------------------------------------------------------

template<class _>
class ws_async_echo_port_impl<_>::connection
    : public std::enable_shared_from_this<connection>
{
    int state = 0;
    ws_async_echo_port_impl& handler_;
    endpoint_type ep_;
    beast::websocket::stream<socket_type> ws_;
    boost::asio::io_service::strand strand_;
    beast::multi_buffer buffer_;
    std::size_t id_;

public:
    connection(connection&&) = default;
    connection(connection const&) = default;
    connection& operator=(connection&&) = delete;
    connection& operator=(connection const&) = delete;

    explicit
    connection(ws_async_echo_port_impl& handler, std::size_t id,
            endpoint_type const& ep, socket_type&& sock);

    void
    run();

private:
    void
    on_accept(error_code ec);

    void
    do_read();
        
    void
    on_read(error_code ec);

    void
    on_write(error_code ec);

    void
    fail(std::string what, error_code ec);
};

//------------------------------------------------------------------------------

template<class _>
ws_async_echo_port_impl<_>::
connection::
connection(ws_async_echo_port_impl& handler, std::size_t id,
        endpoint_type const& ep, socket_type&& sock)
    : handler_(handler)
    , ep_(ep)
    , ws_(std::move(sock))
    , strand_(ws_.get_io_service())
    , id_(id)
{
    handler_.cb_(ws_);
}

template<class _>
void
ws_async_echo_port_impl<_>::
connection::
run()
{
    ws_.async_accept_ex(
        [](beast::websocket::response_type& res)
        {
            res.insert(
                "Server", "async_echo_server");
        },
        strand_.wrap(std::bind(&connection::on_accept,
            shared_from_this(), std::placeholders::_1)));
}

template<class _>
void
ws_async_echo_port_impl<_>::
connection::
on_accept(error_code ec)
{
    if(ec)
        return fail("async_accept", ec);
    do_read();
}

template<class _>
void
ws_async_echo_port_impl<_>::
connection::
do_read()
{
    ws_.async_read(buffer_, strand_.wrap(
        std::bind(&connection::on_read, shared_from_this(),
            std::placeholders::_1)));
}
        
template<class _>
void
ws_async_echo_port_impl<_>::
connection::
on_read(error_code ec)
{
    if(ec == beast::websocket::error::closed)
        return;

    if(ec)
        return fail("on_read", ec);

    ws_.binary(ws_.got_binary());

    ws_.async_write(buffer_.data(), strand_.wrap(
        std::bind(&connection::on_write, shared_from_this(),
            std::placeholders::_1)));
}

template<class _>
void
ws_async_echo_port_impl<_>::
connection::
on_write(error_code ec)
{
    if(ec)
        return fail("on_write", ec);

    buffer_.consume(buffer_.size());

    do_read();
}

template<class _>
void
ws_async_echo_port_impl<_>::
connection::
fail(std::string what, error_code ec)
{
    if(ec != beast::websocket::error::closed)
        handler_.log_ <<
            "[#" << id_ << " " << ep_ << "] " <<
        what << ": " << ec.message() << std::endl;
}

//------------------------------------------------------------------------------

template<class _>
ws_async_echo_port_impl<_>::
ws_async_echo_port_impl(
        std::ostream& log, on_new_stream_cb cb)
    : log_(log)
    , cb_(cb)
{
}

template<class _>
void
ws_async_echo_port_impl<_>::
on_accept(std::size_t id,
    socket_type&& sock, endpoint_type ep)
{
    std::make_shared<connection>(
        *this, id, ep, std::move(sock))->run();
}

} // server

using ws_async_echo_port = server::ws_async_echo_port_impl<>;

#endif
