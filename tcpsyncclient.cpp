#include "tcpsyncclient.h"

void TcpSyncClient::log(std::string message)
{
    std::cout << "[TSC] " << message;
    if (message[message.size() - 1] != '\n') std::cout << std::endl;
}

TcpSyncClient::TcpSyncClient(boost::asio::io_service& service, std::string config) :
                                                          to_read(false),
                                                          to_raw(false),
                                                          to_start(false),
                                                          m_sock(service),
                                                          m_config_file(config)
{
    to_start = read_config();
    if(to_start)
    {
        if (params["channel"][0] != '#') params["channel"] = "#" + params["channel"];

        try {
        boost::asio::ip::tcp::endpoint e(
                    boost::asio::ip::address::from_string(
                        params["address"]), std::stoi(params["port"]) );
        m_ep = e;
        } catch (boost::system::system_error & err) {
            std::cerr << err.what() << ": " << params["address"] << " / "
                      << params["port"] << std::endl;
        }

        log (m_ep.address().to_string());
        log (std::to_string (m_ep.port()));
        log (params["channel"]);
    }
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
    return write("PRIVMSG " + params["channel"] + " " + msg);
}

bool TcpSyncClient::write_to_user(std::string user, std::string msg)
{
    return write("PRIVMSG " + user + " " + msg);
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
    write("NICK " + params["nickname"]);
    read_answer();
    if(params["password"] != "x") {
        write("PRIVMSG NICKSERV IDENTIFY " + params["password"]);
    }
    write("JOIN " + params["channel"]);
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
    }
}

bool TcpSyncClient::read_config()
{
    if (!boost::filesystem::exists(m_config_file)) {
        std::cerr << "Config not found" << std::endl;
        return false;
    }

    boost::property_tree::ptree pt;
    boost::property_tree::read_json(m_config_file, pt);

    for (auto child: pt.get_child("socket"))
    {
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (params[child.first] != "")
            {
                params[child.first] = child.second.get_value<std::string>();
            }
        }
    }
    return true;
}

void TcpSyncClient::process_msg()
{
    std::string msg(m_buff, m_already_read);
    log("[MSG] " + msg);
    if (msg.find("PING :") == 0) answer_to_ping(msg.substr(6));

    // Парсинг сообщений, адресованных боту. Сохраняет ник отправителя и текст.
    if (msg.find("PRIVMSG " + params["channel"] + " :" + params["nickname"]) != std::string::npos)
    {
        m_msg = msg.substr(msg.find(" :" + params["nickname"]) + 3 + params["nickname"].size() );
        while (m_msg[0] == ' ') m_msg = m_msg.substr(1);
        while (m_msg.back() == '\n'|| m_msg.back() == '\r' || m_msg.back() == ' ') m_msg.pop_back();
        m_msg_nickname = msg.substr(1, msg.find('!') - 1);
        to_read = true;
    }

    // Парсинг всех сообщений на канале. Сохраняет ник отправителя и текст.
    else if (msg.find("PRIVMSG " + params["channel"] + " :") != std::string::npos)
    {
        m_raw = msg.substr(msg.find(params["channel"] + " :") + 2 + params["channel"].size());
        m_raw_nickname = msg.substr(1, msg.find('!') - 1);

        while (m_raw[0] == ' ') m_raw = m_raw.substr(1);
        while (m_raw.back() == '\n'|| m_raw.back() == '\r' || m_raw.back() == ' ') m_raw.pop_back();

        if (m_raw.find("ACTION") == 0) {
            m_raw = "-" + m_raw.substr(7);
            while (m_raw.find('') != std::string::npos) m_raw.pop_back();
            m_raw += " -";
        }

        to_raw = true;

        if (m_raw[0] == '.') {
            if (m_raw[1] == '.') to_raw = false; // Максимальная анонимность, нет лога
            m_raw = "**blinded message**";
        }
    }
}

TcpSyncClient::~TcpSyncClient()
{
    std::cout << "TcpSyncClient destructed" << std::endl;
}
