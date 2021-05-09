#include <iostream>
#include "tcpsyncclient.h"

void usage(std::string argv)
{
    std::cout << argv << " <address> <port>" << std::endl;
}

int main(int argc, char * argv[])
{
    if (argc < 2) usage( std::string(argv[0]));

    boost::asio::io_service service;
    boost::asio::ip::tcp::endpoint ep(
                boost::asio::ip::address::from_string(
                    std::string(argv[1])), std::stoi(std::string(argv[2])) );

    TcpSyncClient(ep, service);
    return 0;
}
