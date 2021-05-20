#include "tcpsyncclient.h"

void TcpSyncClient::log(std::string message)
{
    std::cout << "[TSC] " << message;
    if (message[message.size() - 1] != '\n') std::cout << std::endl;
}

TcpSyncClient::TcpSyncClient(boost::asio::ip::tcp::endpoint ep, boost::asio::io_service& s,
                             const std::string c, const std::string n, const std::string p) :
                                                          to_read(false),
                                                          to_raw(false),
                                                          m_ep(ep),
                                                          m_sock(s),
                                                          m_channel(c),
                                                          m_mynick(n),
                                                          m_password(p)
{
    log (ep.address().to_string());
    log (std::to_string (ep.port()) );
    log (m_channel);
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
    return m_msg;
}

std::string TcpSyncClient::get_msg_nick()
{
    return m_msg_nickname;
}

std::string TcpSyncClient::get_raw()
{
    if (!to_raw) return "to_raw is false";
    to_raw = false;
    return m_raw;
}

std::string TcpSyncClient::get_raw_nick()
{
    return m_raw_nickname;
}

bool TcpSyncClient::connect_to_ep()
{
    try {
        m_sock.connect(m_ep);
    } catch (boost::system::system_error & err) {
        std::cerr << err.what() <<std::endl;
        return false;
    }
    return true;
}

size_t TcpSyncClient::read_complete(const error_code& err, size_t bytes) {
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
    write("USER " + m_user + " . . :" + m_realname);
    write("NICK " + m_mynick);
    read_answer();
    if(m_password != "") {
        write("PRIVMSG NICKSERV IDENTIFY " + m_password);
    }
    write("JOIN " + m_channel);
}

void TcpSyncClient::start()
{
    if ( connect_to_ep()) {
        connect_to_server();
        log("[CONNECTED TO SERVER]");
        while (true) read_answer();
    }
    else {
        log("[START FAILED]");
        to_read = true;
        m_msg = ERROR_START_FAILED;
    }
}

void TcpSyncClient::process_msg()
{
    std::string msg(m_buff, m_already_read);
    log("[MSG] " + msg);
    if (msg.find("PING :") == 0) answer_to_ping(msg.substr(6));

    // Парсинг сообщений, адресованных боту. Сохраняет ник отправителя и текст.
    if (msg.find("PRIVMSG " + m_channel + " :" + m_mynick) != std::string::npos)
    {
        m_msg = msg.substr(msg.find(" :" + m_mynick) + 3 + m_mynick.size() );
        while (m_msg[0] == ' ') m_msg = m_msg.substr(1); // Режу первые пробелы
        m_msg_nickname = msg.substr(1, msg.find('!') - 1);
        to_read = true;
    }

    // Парсинг всех сообщений на канале. Сохраняет ник отправителя и текст.
    else if (msg.find("PRIVMSG " + m_channel + " :") != std::string::npos)
    {
        m_raw = msg.substr(msg.find(m_channel + " :") + 2 + m_channel.size());
        m_raw_nickname = msg.substr(1, msg.find('!') - 1);

        while (m_raw[0] == ' ') m_raw = m_raw.substr(1);
        while (m_raw[m_raw.size() - 1] == '\n') m_raw.pop_back();

        if (m_raw.find("ACTION") == 0) {
            m_raw = "-" + m_raw.substr(7);
            while (m_raw.find('') != std::string::npos) m_raw.pop_back();
            m_raw += " -\n";
        }

        to_raw = true;

        if (m_raw[0] == '.') {
            if (m_raw[1] == '.') to_raw = false; // Максимальная анонимность, нет лога
            m_raw = "**blinded message**\n";
        }

    }
}
