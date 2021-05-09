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

class TcpSyncClient
{
public:
    TcpSyncClient(std::string a, std::string p);

private:
    void displayConfig();

    std::string m_address;
    std::string m_port;
};

#endif // TCPSYNCCLIENT_H
