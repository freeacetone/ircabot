#include "tcpsyncclient.h"

template <typename T>
void TcpSyncClient::log(T message)
{
    std::cout << "[TSC] " << message << std::endl;
}

TcpSyncClient::TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& service,
                             const std::string channel) : to_read(false),
                                                          m_ep(ep),
                                                          m_sock(service),
                                                          m_channel(channel)
{
    log(ep.address().to_string());
    log(ep.port());
    log(m_channel);
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

bool TcpSyncClient::write_to_channel(std::string msg)
{
    bool res = write("PRIVMSG " + m_channel + " " + msg);
    return res;
}

std::string TcpSyncClient::get_msg()
{
    if (!to_read) return "to_read is false";
    to_read = false;
    return m_buffer;
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
    read(m_sock, boost::asio::buffer(m_buff, 1024),
                     boost::bind(&TcpSyncClient::read_complete, this, _1, _2));
    process_msg();
}

void TcpSyncClient::answer_to_ping(std::string ping)
{
    write("PONG :" + ping);
    log("[PONG] " + ping);
}

void TcpSyncClient::connect_to_server()
{
    write("USER " + USER + " . . :" + REALNAME);
    write("NICK " + NICK);
    read_answer();
    write("PRIVMSG NICKSERV IDENTIFY 906090");
    read_answer();
    write("JOIN " + m_channel);
}

void TcpSyncClient::loop()
{
    if ( connect_to_ep()) {
        connect_to_server();
        log("Connected to server");
    }
    else log("Error: Connection to Endpoint");
    while (true) {
        read_answer();
    }
}

void TcpSyncClient::process_msg()
{
    std::string msg(m_buff, m_already_read);
    log("[PROCESS] " + msg);
    if (msg.find("PING :") == 0) answer_to_ping(msg.substr(6));

    if (msg.find("PRIVMSG " + m_channel + " :" + NICK) != std::string::npos) {
        m_buffer = msg.substr(msg.find(" :" + NICK) + 3 + NICK.size() );
        while (m_buffer[0] == ' ') m_buffer = m_buffer.substr(1); // Режу первые пробелы

        to_read = true;
        if(m_buffer.find("хозяин") != std::string::npos) // Для дебага
            write_to_channel("a c e t o n e");
    }
}
