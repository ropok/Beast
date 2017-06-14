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

namespace server {

using error_code = boost::system::error_code;
using socket_type = boost::asio::ip::tcp::socket;
using address_type = boost::asio::ip::address_v4;
using endpoint_type = boost::asio::ip::tcp::endpoint;

/** A server instance that accepts TCP/IP connections.

    This is a general purpose TCP/IP server which contains
    zero or more user defined "ports". Each port represents
    a listening socket whose behavior is defined by an
    instance of the @b PortHandler concept.

    To use the server, construct the class and then add the
    ports that you want using @ref make_port.

    @par Example

    @code

    // Create a server with 4 threads 
    //
    server::instance si(4);

    // Create a port that echoes everything back.
    // Bind all available interfaces on port 1000.
    //
    server::error_code ec;
    si.make_port<echo_port>(
        ec,
        server::endpoint_type{
            server::address_type::from_string("0.0.0.0"), 1000}
    );

    ...

    // Close all connections, shut down the server
    si.stop();

    @endcode
*/
template<class = void>
class instance_impl
{
    class port_base
    {
    public:
        virtual ~port_base() = default;
        virtual void close() = 0;
    };

    template<class PortHandler>
    class port;

    boost::asio::io_service ios_;
    std::vector<std::thread> threads_;
    std::vector<std::shared_ptr<port_base>> ports_;
    boost::optional<boost::asio::io_service::work> work_;

public:
    instance_impl(instance_impl const&) = delete;
    instance_impl& operator=(instance_impl const&) = delete;

    /** Constructor

        @param n The number of threads to run on the `io_service`,
        which must be greater than zero.
    */
    explicit
    instance_impl(std::size_t n = 1);

    /** Destructor

        The destructor will block until all asynchronous I/O
        has completed.
    */
    ~instance_impl();

    /// Return the `io_service` associated with the instance_impl
    boost::asio::io_service&
    get_io_service()
    {
        return ios_;
    }

    /// Return a new, small integer unique id
    std::size_t
    next_id()
    {
        static std::atomic<std::size_t> id_{0};
        return ++id_;
    }

    /** Create a listening port.

        @param ec Set to the error, if any occurred.

        @param ep The address and port to bind to.

        @param args Optional arguments, forwarded to the
        port handler's constructor.

        @tparam PortHandler The port handler to use for handling
        incoming connections on this port. This handler must meet
        the requirements of @b PortHandler. A model of PortHandler
        is as follows:

        @code

        struct PortHandler
        {
            void
            on_accept(
                std::size_t id,         // a small, unique id for the connection
                socket_type&& sock,     // the connected socket
                endpoint_type ep        // address of the remote endpoint
            );
        };

        @endcode
    */
    template<
        class PortHandler,
        class... Args>
    void
    make_port(
        error_code& ec,
        boost::asio::ip::tcp::endpoint const& ep,
        Args&&... args);

    /** Stop the instance.

        This function call returns immediately. Connections
        are stopped on the corresponding `io_service` threads.
    */
    void
    stop();
};

//------------------------------------------------------------------------------

template<class _>
template<class PortHandler>
class instance_impl<_>::port
    : public port_base
    , public std::enable_shared_from_this<port<PortHandler>>
{
    instance_impl& instance_;
    PortHandler handler_;
    boost::asio::io_service::strand strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::ip::tcp::endpoint ep_;

public:
    template<class... Args>
    port(instance_impl& instance, Args&&... args);

    ~port();
    PortHandler&
    handler()
    {
        return handler_;
    }

    void
    open(error_code& ec,
        boost::asio::ip::tcp::endpoint const& ep);

    void
    close() override;

private:
    void
    on_accept(error_code ec);
};

template<class _>
template<class PortHandler>
template<class... Args>
instance_impl<_>::
port<PortHandler>::
port(instance_impl& instance, Args&&... args)
    : instance_(instance)
    , handler_(std::forward<Args>(args)...)
    , strand_(instance_.get_io_service())
    , acceptor_(instance_.get_io_service())
    , sock_(instance_.get_io_service())
{
}

template<class _>
template<class PortHandler>
instance_impl<_>::
port<PortHandler>::
~port()
{
}

template<class _>
template<class PortHandler>
void
instance_impl<_>::
port<PortHandler>::
open(error_code& ec,
    boost::asio::ip::tcp::endpoint const& ep)
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
        std::bind(&port::on_accept, this->shared_from_this(),
            std::placeholders::_1));
}

template<class _>
template<class PortHandler>
void
instance_impl<_>::
port<PortHandler>::
close()
{
    if(! strand_.running_in_this_thread())
        acceptor_.get_io_service().post(
            strand_.wrap(std::bind(&port::close,
                this)));
    error_code ec;
    acceptor_.close(ec);
}

template<class _>
template<class PortHandler>
void
instance_impl<_>::
port<PortHandler>::
on_accept(error_code ec)
{
    if(! acceptor_.is_open())
        return;
    if(ec == boost::asio::error::operation_aborted)
        return;
    if(! ec)
        handler_.on_accept(
            instance_.next_id(), std::move(sock_), ep_);
    acceptor_.async_accept(sock_, ep_,
        std::bind(&port::on_accept, this->shared_from_this(),
            std::placeholders::_1));
}

//------------------------------------------------------------------------------

template<class _>
instance_impl<_>::
instance_impl(std::size_t n)
    : work_(ios_)
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

template<class _>
instance_impl<_>::
~instance_impl()
{
    work_ = boost::none;
    stop();
    for(auto& t : threads_)
        t.join();
}

template<class _>
template<class PortHandler, class... Args>
void
instance_impl<_>::
make_port(error_code& ec,
    boost::asio::ip::tcp::endpoint const& ep,
    Args&&... args)
{
    auto sp = std::make_shared<port<PortHandler>>(
        *this, std::forward<Args>(args)...);
    sp->open(ec, ep);
    if(ec)
        return;
    ports_.emplace_back(std::move(sp));
    /*
    return std::shared_ptr<PortHandler>{
        sp, &sp->handler()};
    */
}

// VFALCO This could take a timeout parameter if the timeout
//        expires then close the connections. Of course this
//        requires that the instance_impl manage I/O objects...
template<class _>
void
instance_impl<_>::
stop()
{
    for(auto const& port : ports_)
        port->close();
    ports_.clear();
}

using instance = instance_impl<>;

} // server

#endif
