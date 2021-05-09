#include "tcpsyncclient.h"

TcpSyncClient::TcpSyncClient(std::string a, std::string p) : m_address(a), m_port(p)
{
    this->displayConfig();
}

void TcpSyncClient::displayConfig()
{
    std::cout << "Address: " << m_address << std::endl;
    std::cout << "Port: " << m_port << std::endl;
}
