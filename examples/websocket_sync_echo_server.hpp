//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef WEBSOCKET_SYNC_ECHO_SERVER_HPP
#define WEBSOCKET_SYNC_ECHO_SERVER_HPP

#include "server.hpp"

#include <beast/core/multi_buffer.hpp>
#include <beast/websocket.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

namespace server {

template<class = void>
class ws_sync_echo_port_impl
{
    using on_new_stream_cb = std::function<
        void(beast::websocket::stream<socket_type>&)>;

    std::ostream& log_;
    on_new_stream_cb cb_;

public:
    ws_sync_echo_port_impl(
        std::ostream& log, on_new_stream_cb cb);

    void
    on_accept(std::size_t id,
        socket_type&& sock, endpoint_type ep);

private:
    void
    do_connection(std::size_t id,
        endpoint_type const& ep, socket_type&& sock);
};

template<class _>
ws_sync_echo_port_impl<_>::
ws_sync_echo_port_impl(
        std::ostream& log, on_new_stream_cb cb)
    : log_(log)
    , cb_(cb)
{
}

template<class _>
void
ws_sync_echo_port_impl<_>::
on_accept(std::size_t id,
    socket_type&& sock, endpoint_type ep)
{
    // This work-around is for C++11
    // missing lambda capture assignment.
    struct lambda
    {
        std::size_t id;
        endpoint_type ep;
        ws_sync_echo_port_impl& self;
        boost::asio::io_service::work work;
        // Must be destroyed before work otherwise the
        // io_service could be destroyed before the socket.
        socket_type sock;

        lambda(ws_sync_echo_port_impl& self_,
            std::size_t id_, endpoint_type const& ep_,
                socket_type&& sock_)
            : id(id_)
            , ep(ep_)
            , self(self_)
            , work(sock_.get_io_service())
            , sock(std::move(sock_))
        {
        }

        void operator()()
        {
            self.do_connection(id, ep, std::move(sock));
        }
    };
    std::thread{lambda{
        *this, id, ep, std::move(sock)}}.detach();
}

template<class _>
void
ws_sync_echo_port_impl<_>::
do_connection(std::size_t id,
    endpoint_type const& ep, socket_type&& sock)
{
    error_code ec;
    auto const fail =
        [&](std::string const& what)
        {
            if(ec != beast::websocket::error::closed)
                this->log_ <<
                    "[#" << id << " " << ep << "] " <<
                    what << ": " << ec.message() << std::endl;
        };

    beast::websocket::stream<
        socket_type> ws{std::move(sock)};
    cb_(ws);
    ws.accept_ex(
        [](beast::websocket::response_type& res)
        {
            res.insert(beast::http::field::server,
                "websocket_sync_echo_server");
        },
        ec);
    if(ec)
    {
        fail("accept");
        return;
    }
    for(;;)
    {
        beast::multi_buffer b;
        ws.read(b, ec);
        if(ec)
            return fail("read");
        ws.binary(ws.got_binary());
        ws.write(b.data(), ec);
        if(ec)
            return fail("write");
    }
        
}

} // server

using ws_sync_echo_port = server::ws_sync_echo_port_impl<>;

#endif
