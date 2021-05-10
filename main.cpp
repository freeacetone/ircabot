#include <iostream>
#include "tcpsyncclient.h"

boost::asio::io_service service;

void usage(std::string path)
{
    std::cout << "Usage:\n" << path << " <address> <port> <#channel>" << std::endl;
}

int main(int argc, char * argv[])
{
////// Проверка переданных данных
    if (argc < 4) {
        usage( std::string(argv[0]));
        return -1;
    }

    std::string address(argv[1]);
    std::string port(argv[2]);
    std::string channel(argv[3]);

    if (argv[3][0] != '#') {
        std::cerr << "Incorrect channel name. Maybe '#" << channel << "'?" << std::endl;
        return -2;
    }

    try { // Проверка переданных адрес:порт
    boost::asio::ip::tcp::endpoint ep(
                boost::asio::ip::address::from_string(
                    address), std::stoi(port) );

    } catch (boost::system::system_error & err) {
        std::cerr << err.what() << ": " << address << " / "
                  << port << std::endl;
        return -3;
    }

////// Начало работы
    boost::asio::ip::tcp::endpoint ep(
                boost::asio::ip::address::from_string(
                    address), std::stoi(port) );
    TcpSyncClient socket(ep, service, channel);

    return 0;
}
