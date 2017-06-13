//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_EXAMPLES_SYNC_SERVER_HPP
#define BEAST_EXAMPLES_SYNC_SERVER_HPP

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

/** A server that accepts TCP/IP connections.
*/
template<class Derived>
class server
{
    class port_base
    {
        virtual ~port_base() = default;
        virtual void close() = 0;
    };

    template<class AcceptHandler>
    class port;

    boost::asio::io_service ios_;
    std::vector<std::thread> threads_;
    boost::optional<boost::asio::io_service::work> work_;

protected:
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;

public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;

    server(server const&) = delete;
    server& operator=(server const&) = delete;

    /** Constructor

        @param n The number of threads to run on the `io_service`,
        which must be greater than zero.
    */
    explicit
    server(std::size_t n = 1);

    /// Destructor
    ~server();

    /// Return the `io_service` associated with the server
    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    /** Return the listening endpoint.
    */
    boost::asio::ip::tcp::endpoint
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

    /** Open a listening port.

        @param ep The address and port to bind to.

        @param ec Set to the error, if any occurred.

        @param handler The handler to associate with the listening
        port, which will be copied or moved. The handler must have
        this equivalent signature:
        @code
        void handler(
            socket_type&& sock,     // The accepted socket
            endpoint_type ep        // The remote endpoint
        );
        @endcode
    */
    template<class AcceptHandler>
    void
    open(boost::asio::ip::tcp::endpoint const& ep,
        error_code& ec, AcceptHandler&& handler);

    /** Stop the server.
    */
    // VFALCO This could take a timeout parameter if the timeout
    //        expires then close the connections. Of course this
    //        requires that the server manage I/O objects...
    void
    stop();

private:
    Derived&
    impl()
    {
        return static_cast<Derived&>(*this);
    }

    void
    on_accept(error_code ec);
};

//------------------------------------------------------------------------------

template<class AcceptHandler>
class server::port : public port_base
{
    server& s_;
    AcceptHandler hander_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::ip::tcp::endpoint ep_;
    boost::asio::ip::tcp::acceptor acceptor_;

public:
    template<class DeducedHandler>
    port(server& s, DeducedHandler&& handler)
        : s_(s)
        , handler_(std::forward<DeducedHandler>(handler))
        , sock_(s_.get_io_service())
        , acceptor(s_.get_io_service())
    {
    }

    void
    open(boost::asio::ip::tcp::endpoint const& ep)
    {
        acceptor_.open(ep.protocol(), ec);
        if(ec)
            return;
        acceptor_.set_option(
            boost::asio::socket_base::reuse_address{true});
        acceptor_.bind(ep, ec);
        if(ec)
            return;
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        if(ec)
            return;
        acceptor_.async_accept(sock_, ep_,
            std::bind(&server::on_accept, this,
                std::placeholders::_1));
    }

    void
    close() override
    {
        error_code ec;
        acceptor_.close(ec);
    }

private:
    void
    on_accept(error_code ec)
    {
        if(! acceptor_.is_open())
            return;
        if(ec == boost::asio::error::operation_aborted)
            return;
        if(! ec)
            handler_(std::move(sock_), ep_);
        acceptor_.async_accept(sock_, ep_,
            std::bind(&server::on_accept, this,
                std::placeholders::_1));
    }
};

//------------------------------------------------------------------------------

template<class Derived>
server<Derived>::
server(std::size_t n)
    : sock_(ios_)
    , acceptor_(ios_)
    , work_(ios_)
{
    if(n < 1)
        throw std::invalid_argument{"threads < 1"};
    threads_.reserve(n);
    while(n--)
        threads_.emplace_back(
            [&]
            {
                ios_.run();
            });
}

template<class Derived>
server<Derived>::
~server()
{
    work_ = boost::none;
    ios_.dispatch(
        [&]
        {
            error_code ec;
            acceptor_.close(ec);
        });
    for(auto& t : threads_)
        t.join();
}

template<class Derived>
void
server<Derived>::
open(boost::asio::ip::tcp::endpoint const& ep,
    error_code& ec)
{
    boost::asio::ip::tcp::acceptor acceptor{ios_};
    acceptor.open(ep.protocol(), ec);
    if(ec)
        return;
    acceptor.set_option(
        boost::asio::socket_base::reuse_address{true});
    acceptor.bind(ep, ec);
    if(ec)
        return;
    acceptor.listen(
        boost::asio::socket_base::max_connections, ec);
    if(ec)
        return;
    using std::swap;
    swap(acceptor, acceptor_);
    acceptor_.async_accept(sock_, ep_,
        std::bind(&server::on_accept, this,
            std::placeholders::_1));
}

template<class Derived>
void
server<Derived>::
stop()
{
    ios_.post(
        [&]()
        {
            error_code ec;
            acceptor_.close(ec);
        });
}

template<class Derived>
void
server<Derived>::
on_accept(error_code ec)
{
    if(! acceptor_.is_open())
        return;
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(! ec)
        impl().do_accept(std::move(sock_), ep_);
    acceptor_.async_accept(sock_, ep_,
        std::bind(&server::on_accept, this,
            std::placeholders::_1));
}


#endif
