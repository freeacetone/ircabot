#include "tcpsyncclient.h"

template <typename T>
void TcpSyncClient::log(T message)
{
    std::cout << "[TSC] " << message << std::endl;
}

TcpSyncClient::TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& service,
                             const std::string channel) : m_started(true),
                                                          m_ep(ep), m_sock(service),
                                                          m_channel(channel)

{
    log(ep.address().to_string());
    log(ep.port());
    log(m_channel);

    connect_to_ep(); //FIXME return code ?
    connect_to_server();

}

bool TcpSyncClient::write(std::string msg)
{
    try {
        m_sock.write_some(boost::asio::buffer(msg + '\n'));
    } catch (boost::system::system_error & err) {
        log("Write("+msg+") error");
        std::cerr << err.what() <<std::endl;
        return false;
    }
    return true;
}

bool TcpSyncClient::connect_to_ep()
{
    try {
        m_sock.connect(m_ep);
    } catch (boost::system::system_error & err) {
        log("Connect_to_socket error");
        std::cerr << err.what() <<std::endl;
        return false;
    }
    return true;
}

size_t TcpSyncClient::read_complete(const error_code & err, size_t bytes) {
    if ( err) return 0;
    m_already_read = bytes;
    bool found = std::find(m_buff, m_buff + bytes, '\n') < m_buff + bytes;
    return found ? 0 : 1;
}

void TcpSyncClient::read_answer()
{
    m_already_read = 0;
    read(m_sock, boost::asio::buffer(m_buff),
                     boost::bind(&TcpSyncClient::read_complete, this, _1, _2));
    process_msg();
}

void TcpSyncClient::answer_to_ping(std::string ping)
{
    write("PONG :" + ping);
}

bool TcpSyncClient::connect_to_server()
{
    write("USER " + USER + " . . :" + REALNAME);
    write("NICK " + NICK);
    read_answer();

    return true;
}

void TcpSyncClient::loop()
{

}

void TcpSyncClient::process_msg()
{
    std::string msg(m_buff, m_already_read);
    log("Readed: " + msg);
    if (msg.find("PING :") == 0) answer_to_ping(msg.substr(6));
}
