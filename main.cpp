#include <iostream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <boost/filesystem.hpp>
#include "tcpsyncclient.h"

boost::asio::io_service service;
TcpSyncClient * volatile tsc = nullptr;
std::string channel;
std::string nick;
std::string password;
boost::asio::ip::tcp::endpoint ep;

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

    if (boost::filesystem::exists("/var/www/html/doc/irc-log")) // unix, продакшен
    {
        if (! boost::filesystem::exists("/var/www/html/doc/irc-log/" + year + "/" + month))
        {
            boost::filesystem::create_directories("/var/www/html/doc/irc-log/" + year + "/" + month);
        }
        out.open("/var/www/html/doc/irc-log/" + year + "/" + month + "/" + day + ".txt", std::ios::app);
        if (! out.is_open()) return 1;
    }
    else if (boost::filesystem::exists("D:\\irc-log")) // win, тесты
    {
        if (! boost::filesystem::exists("D:\\irc-log\\" + year + "\\" + month))
        {
            boost::filesystem::create_directories("D:\\irc-log\\" + year + "\\" + month);
        }
        out.open("D:\\irc-log\\" + year + "\\" + month + "\\" + day + ".txt", std::ios::app);
        if (! out.is_open()) return 1;
    }
    else return 2;

    out << year << "-" << month << "-" << day << " " << msg;
    out.close();
    return 0;
}

void usage(std::string path)
{
    std::cout << "IRC abot usage:\n" << path << " <address> <port> <#channel> <nickname> [<password>]" << std::endl;
}

void make_tsc()
{
    tsc = new TcpSyncClient(ep, service, channel, nick, password);
    tsc->start();
}

int main(int argc, char * argv[])
{
//// Проверка переданных данных
    if (argc < 5) {
        std::cout << argc << std::endl;
        usage( std::string(argv[0]));
        return 1;
    }

    channel = std::string(argv[3]);
    if (argv[3][0] != '#') {
        channel = "#" + channel;
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
        return 3;
    }

    nick = std::string(argv[4]);
    if (argv[5] != nullptr) password = std::string(argv[5]);

//////
    std::thread connection(make_tsc);

    while (tsc == nullptr) std::this_thread::sleep_for(std::chrono::seconds(1));

    while(true) {
        if(tsc->to_read) { // Есть сообщения, адресованные боту
            std::string msg = tsc->get_msg();
            if(msg != ERROR_START_FAILED) {
                tsc->write_to_channel(tsc->get_msg_nick() + ", лог чата: http://acetone.i2p/doc/irc-log/ #" +
                                                            " http://[324:9de3:fea4:f6ac::ace]/doc/irc-log/ #" +
                                                            " http://acetonemadzhxzi2e5tomavam6xpucdfwn2g35vrsz6izgaxv5bmuhad.onion/doc/irc-log/");
            }
            else break;
        }
        if(tsc->to_raw) { // Все сообщения на канале
            std::string raw = tsc->get_raw();
            int res = write_log ("[" + tsc->get_raw_nick() + "] " + raw);
            if (res) tsc->write("PRIVMSG acetone Can't write to log. Error: " + std::to_string(res));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    connection.join();
    return 4; // Выход по обрыву цикла
}
