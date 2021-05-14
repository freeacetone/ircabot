#include <iostream>
#include <thread>
#include "tcpsyncclient.h"

boost::asio::io_service service;
TcpSyncClient * volatile tsc = nullptr;
std::string channel;
std::string nick;
std::string password;
boost::asio::ip::tcp::endpoint ep;

void usage(std::string path)
{
    std::cout << "IRC abot usage:\n" << path << " <address> <port> <#channel> <nickname> [<password>]" << std::endl;
}

void make_tsc()
{
    tsc = new TcpSyncClient(ep, service, channel, nick, password);
    tsc->start();
}

int main(int argc, char * argv[])
{
////// Проверка переданных данных
    if (argc < 5) {
        usage( std::string(argv[0]));
        return -1;
    }

    channel = std::string(argv[3]);
    if (argv[3][0] != '#') {
        std::cerr << "Incorrect channel name. Maybe '#" << channel << "'?" << std::endl;
        return -2;
    }

    std::string address(argv[1]);
    std::string port(argv[2]);

    try {
    boost::asio::ip::tcp::endpoint e(
                boost::asio::ip::address::from_string(
                    address), std::stoi(port) );
    ep = e;

    } catch (boost::system::system_error & err) {
        std::cerr << err.what() << ": " << address << " / "
                  << port << std::endl;
        return -3;
    }

    nick = std::string(argv[4]);
    if (argv[5] != nullptr) password = std::string(argv[5]);

//////
    std::thread connection(make_tsc);

    while (tsc == nullptr) std::this_thread::sleep_for(std::chrono::seconds(1));

    while(true) {
        if(tsc->to_read) {
            std::string msg = tsc->get_msg();
            if(msg != ERROR_START_FAILED) tsc->write_to_channel("[" + tsc->get_msg_nick() + "] " + msg);
            else break;
        }
        if(tsc->to_raw) {
            std::string raw = tsc->get_raw();
            tsc->write_to_channel(tsc->get_raw_nick() + " > " + raw);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    connection.join();
    return -4; // Выход по обрыву цикла
}
