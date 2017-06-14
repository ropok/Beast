//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "websocket_async_echo_server.hpp"
#include "websocket_sync_echo_server.hpp"
#include <boost/asio/io_service.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>

/// Block until SIGINT or SIGTERM is received.
void
sig_wait()
{
    boost::asio::io_service ios;
    boost::asio::signal_set signals(
        ios, SIGINT, SIGTERM);
    signals.async_wait(
        [&](boost::system::error_code const&, int)
        {
        });
    ios.run();
}

class set_stream_options
{
    beast::websocket::permessage_deflate pmd_;

public:
    set_stream_options(set_stream_options const&) = default;

    set_stream_options(
            beast::websocket::permessage_deflate const& pmd)
        : pmd_(pmd)
    {
    }

    template<class NextLayer>
    void
    operator()(beast::websocket::stream<NextLayer>& ws) const
    {
        ws.auto_fragment(false);
        ws.set_option(pmd_);
        ws.read_message_max(64 * 1024 * 1024);
    }
};

int main()
{
    using namespace beast::websocket;

    beast::error_code ec;

    permessage_deflate pmd;
    pmd.client_enable = true;
    pmd.server_enable = true;
    pmd.compLevel = 3;

    server::instance s{1};

    s.make_port<ws_async_echo_port>(
        ec,
        server::endpoint_type{
            server::address_type::from_string("127.0.0.1"), 1000},
        std::cout,
        set_stream_options{pmd});

    s.make_port<ws_sync_echo_port>(
        ec,
        server::endpoint_type{
            server::address_type::from_string("127.0.0.1"), 1001},
        std::cout,
        set_stream_options{pmd});

    sig_wait();

    s.stop();
}
