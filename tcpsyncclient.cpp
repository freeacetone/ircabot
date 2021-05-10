#include "tcpsyncclient.h"

TcpSyncClient::TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& service,
                             const std::string channel) : m_started(true), m_ep(ep), m_sock(service),
                             m_channel(channel)
{
    log(ep.address().to_string());
    log(ep.port());
    log(m_channel);

    bool connectStatus = connect_to_server();
    if(!connectStatus) {
        log("Connection error");
    }
}

bool TcpSyncClient::connect_to_server()
{
    m_sock.connect(m_ep);
    return true;
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
