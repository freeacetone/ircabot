#include <iostream>
#include <thread>
#include "tcpsyncclient.h"

boost::asio::io_service service;
TcpSyncClient * volatile tsc = nullptr;
std::string channel;
boost::asio::ip::tcp::endpoint ep;

void usage(std::string path)
{
    std::cout << "IRC abot usage:\n" << path << " <address> <port> <#channel>" << std::endl;
}

void make_tsc()
{
    tsc = new TcpSyncClient(ep, service, channel);
    tsc->loop();
}

int main(int argc, char * argv[])
{
////// Проверка переданных данных
    if (argc < 4) {
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

//////
    std::thread connection(make_tsc);

    while (tsc == nullptr) std::this_thread::sleep_for(std::chrono::seconds(1));

    while(true) {
        if(tsc->to_read) {
            std::string msg = tsc->get_msg();
            tsc->write_to_channel("[REPLY] " + msg);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    connection.join();
    return 0;
}
