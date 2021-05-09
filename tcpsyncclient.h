#ifndef TCPSYNCCLIENT_H
#define TCPSYNCCLIENT_H

#include <iostream>
#include <boost/bind/bind.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using namespace boost::placeholders;
using boost::system::error_code;

class TcpSyncClient
{
public:
    TcpSyncClient(boost::asio::ip::tcp::endpoint, boost::asio::io_service&);

private:
    template <typename T>
    void log(T);
    size_t read_complete(char*, const error_code&, size_t);

    boost::asio::ip::tcp::endpoint m_ep;
    boost::asio::ip::tcp::socket m_sock;
};

#endif // TCPSYNCCLIENT_H
