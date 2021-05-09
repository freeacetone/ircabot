#include "tcpsyncclient.h"

TcpSyncClient::TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& service) :
    m_ep(ep), m_sock(service)
{
    log(ep.address().to_string());
    log(ep.port());

    // m_sock.connect(ep);

}

template <typename T>
void TcpSyncClient::log(T message)
{
    std::cout << "[TSC] " << message << std::endl;
}

size_t TcpSyncClient::read_complete(char * buf, const error_code & err, size_t bytes) {
    if ( err) return 0;
    bool found = std::find(buf, buf + bytes, '\n') < buf + bytes;
    return found ? 0 : 1;
}
