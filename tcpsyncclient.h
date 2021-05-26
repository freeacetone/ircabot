#ifndef TCPSYNCCLIENT_H
#define TCPSYNCCLIENT_H

#include <iostream>
#include <map>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/filesystem.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

using boost::system::error_code;

class TcpSyncClient
{
public:
    TcpSyncClient(boost::asio::io_service& s, std::string config);
    void start();                                 // Запуск бота
    bool write(std::string);                      // Написать в сокет
    bool write_to_user(std::string, std::string); // Написать пользователю
    bool write_to_channel(std::string);           // Написать в целевой чат
    std::string get_msg();                        // Ник сообщения боту
    std::string get_msg_nick();                   // Сообщение боту
    std::string get_raw();                        // Ник сообщения на канале
    std::string get_raw_nick();                   // Сообщение на канале
    bool have_pm_from_user(std::string);          // Для получения сырого сообщения
    ~TcpSyncClient();

    bool to_read; // Индикаторы наличия информации для чтения
    bool to_raw;
    bool to_start;

    std::map<std::string, std::string> params =
    {
        { "address",  "127.0.0.1"     },
        { "port",     "6667"          },
        { "channel",  "#general"      },
        { "nickname", "bot"           },
        { "password", "x"             }
    };

private:
    void log(std::string);
    bool connect_to_ep();
    size_t read_complete(const error_code&, size_t);
    void read_answer();
    void process_msg();
    void answer_to_ping(std::string);
    void connect_to_server();
    bool read_config();

    int m_already_read;
    char m_buff[512];
    boost::asio::ip::tcp::endpoint m_ep;
    boost::asio::ip::tcp::socket m_sock;

    std::string m_msg;
    std::string m_msg_nickname;
    std::string m_raw;
    std::string m_raw_nickname;
    std::string raw_msg_from_socket;

    std::string m_config_file = "ircbot.json";

    const std::string m_user = "acetonebot";
    const std::string m_realname = "IRC bot in C++";
};

#endif // TCPSYNCCLIENT_H
