#ifndef TCPSYNCCLIENT_H
#define TCPSYNCCLIENT_H

#include <iostream>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

using boost::system::error_code;
const std::string ERROR_START_FAILED = "/as/xk]1pduJfnskAZDD";

class TcpSyncClient
{
public:
    TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& s,
                                 const std::string c, const std::string n, const std::string p);
    void start(); // Запуск бота
    bool write(std::string); // Написать в сокет
    bool write_to_channel(std::string); // Написать в целевой чат
    std::string get_msg();
    std::string get_msg_nick();
    std::string get_raw();
    std::string get_raw_nick();

    bool to_read; // Индикаторы наличия информации для чтения
    bool to_raw;

private:
    void log(std::string);
    bool connect_to_ep();
    size_t read_complete(const error_code&, size_t);
    void read_answer();
    void process_msg();

    int m_already_read;
    char m_buff[512];
    boost::asio::ip::tcp::endpoint m_ep;
    boost::asio::ip::tcp::socket m_sock;

    std::string m_channel;
    void answer_to_ping(std::string);
    void connect_to_server();
    std::string m_msg;
    std::string m_msg_nickname;
    std::string m_raw;
    std::string m_raw_nickname;

    const std::string m_user = "acetonebot";
    const std::string m_realname = "IRC bot in C++";
    const std::string m_mynick;
    const std::string m_password;
};

#endif // TCPSYNCCLIENT_H
