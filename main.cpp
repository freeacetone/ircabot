#include <iostream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <boost/filesystem.hpp>
#include "tcpsyncclient.h"

boost::asio::io_service service;
std::string config_file = "ircbot.json";
TcpSyncClient * volatile tsc = nullptr;
bool tsc_created = false;

std::map<std::string, std::string> conf =
{
    { "admin"  , "x"             },
    { "logpath", "x"             }
};

bool read_config()
{
    if (!boost::filesystem::exists(config_file)) {
        std::cerr << "Config not found" << std::endl;
        return false;
    }

    boost::property_tree::ptree pt;
    boost::property_tree::read_json(config_file, pt);

    for (auto child: pt.get_child("handler"))
    {
        for (size_t i = 0; i < conf.size(); ++i)
        {
            if (conf[child.first] != "")
            {
                conf[child.first] = child.second.get_value<std::string>();
            }
        }
    }
    return true;
}

int write_log(std::string msg)
{
    time_t now = time(0); // Парсинг год/месяц/день
    tm *gmtm = gmtime(&now);
    std::string year = std::to_string (1900 + gmtm->tm_year);
    std::string month = std::to_string (1 + gmtm->tm_mon);
    month.shrink_to_fit();
    if (month.size() < 2) month = "0" + month;
    std::string day = std::to_string(gmtm->tm_mday);
    day.shrink_to_fit();
    if (day.size() < 2) day = "0" + day;

    std::ofstream out;

#ifdef WIN32
    std::string slash = "\\";
#else
    std::string slash = "/";
#endif

    if (boost::filesystem::exists(conf["logpath"]))
    {
        if (! boost::filesystem::exists(conf["logpath"] + slash + year + slash + month))
        {
            boost::filesystem::create_directories(conf["logpath"] + slash + year + slash + month);
        }
        out.open (conf["logpath"] + slash + year + slash + month + slash + day + ".txt", std::ios::app);
        if (! out.is_open()) return 1;
    }
    else return 2;

    out << year << "-" << month << "-" << day << " " << msg;
    out.close();
    return 0;
}

void usage(std::string path)
{
    std::cout << path << " path/to/config.json" << std::endl;
}

void make_tsc()
{
    tsc = new TcpSyncClient(service, config_file);
    for(int i=0; i<2; ++i)
    {
        if (tsc != nullptr)
        {
            if (tsc->to_start)
            {
                tsc->start();
                tsc_created = true;
                return;
            }
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    std::cerr << "make_tsc() time" << std::endl;
    if (tsc != nullptr) delete tsc;
}

int main(int argc, char * argv[])
{
    if (argc >= 2) config_file = static_cast<std::string>(argv[1]);

    if (!read_config()) return 1;

    std::thread connection(make_tsc);

    if (!tsc_created) std::this_thread::sleep_for(std::chrono::seconds(1));

    while (true) {
        if(tsc->to_read) { // Есть сообщения, адресованные боту
            std::string msg = tsc->get_msg();
            if(msg != ERROR_START_FAILED) {
                tsc->write_to_channel(tsc->get_msg_nick() +
                                      ", лог чата: http://acetone.i2p/doc/irc-log/ #" +
                                      " http://[324:9de3:fea4:f6ac::ace]/doc/irc-log/ #" +
                                      " http://acetonemadzhxzi2e5tomavam6xpucdfwn2g35vrsz6izgaxv5bmuhad.onion/doc/irc-log/");
            }
            else break;
        }
        if(tsc->to_raw) { // Все сообщения на канале
            std::string raw = tsc->get_raw();
            int res = write_log ("[" + tsc->get_raw_nick() + "] " + raw);
            if (res) {
                tsc->write("PRIVMSG " + conf["admin"] +
                        " Can't write to log. Error: " + std::to_string(res));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    connection.join();
    return 2; // Выход по обрыву цикла
}
