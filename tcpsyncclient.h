#ifndef TCPSYNCCLIENT_H
#define TCPSYNCCLIENT_H

#include <iostream>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

const static std::string USER = "acetonebot";
const static std::string NICK = "abot";
const static std::string REALNAME = "IRC bot in C++";

using boost::system::error_code;

class TcpSyncClient
{
public:
    TcpSyncClient(boost::asio::ip::tcp::endpoint, boost::asio::io_service&, const std::string);
    bool write(std::string);

    bool started;

private:
    template <typename T>
    void log(T);
    bool connect_to_ep();
    size_t read_complete(const error_code&, size_t);
    void read_answer();
    void process_msg();
    void loop();

    int m_already_read;
    char m_buff[1024]; // Буффер 1Кб
    boost::asio::ip::tcp::endpoint m_ep;
    boost::asio::ip::tcp::socket m_sock;

    std::string m_channel;
    void answer_to_ping(std::string);
    bool connect_to_server();
};

#endif // TCPSYNCCLIENT_H
