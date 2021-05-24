#include <iostream>
#include <fstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <vector>
#include <thread>
#include <boost/filesystem.hpp>
#include "tcpsyncclient.h"

boost::asio::io_service service;
std::string config_file = "ircbot.json";
TcpSyncClient * volatile tsc = nullptr;
bool tsc_created = false;

std::map<std::string, std::string> conf =
{
    { "admin"   , "" },
    { "logpath" , "" },
    { "find"    , "" },
    { "notfound", "" },
    { "findzero", "" }
};

std::string search(std::string text)
{
    constexpr int maxSize = 10;
    std::string values;
    std::vector<std::string> matches;
    std::regex regex(".*" + text + ".*", std::regex_constants::extended | std::regex_constants::icase);

    boost::filesystem::recursive_directory_iterator dir(conf["logpath"]), end;
    uint64_t success = 0;
    for (; dir != end; ++dir)
    {
        if (! boost::filesystem::is_directory(dir->path()))
        {
            std::string buffer;
            std::ifstream log(dir->path().c_str());

            while(getline(log, buffer))
            {
                if (std::regex_match(buffer, regex))
                {
                    ++success;
                    std::string date = buffer.substr(0, buffer.find(' '));

                    bool first = true;
                    for (auto entry: matches)
                    {
                        if (entry.find(date) != std::string::npos) first = false;
                    }
                    if (first) matches.push_back(date);
                }
            }
            log.close();
        }
    }
    if (matches.size() > 0)
    {
        std::reverse(matches.begin(), matches.end());
        values += "(" + std::to_string(success) + ") ";

        for (int i = matches.size()-1, count = 0; i >= 0 && count < maxSize ; --i, ++count)
        { // Компоновка выходной строки
            if (values.find('-') != std::string::npos) values += ", ";
            values += matches[i];
        }
        if (values != "") values += ".";

        for (auto it = values.begin(), end = values.end(); it != end; ++it)
        { // Замена тире на слеш
            if (*it == '-') *it = '/';
        }
    }
    return values;
}

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
             conf[child.first] = child.second.get_value<std::string>();
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

void handler()
{
    if (!tsc_created) std::this_thread::sleep_for(std::chrono::seconds(1));
    bool handled = false;

    while (true)
    {
        if(tsc->to_read) { // Есть сообщения, адресованные боту
            std::string msg = tsc->get_msg();

            if (tsc->get_msg_nick() == conf["admin"] && (msg.find("reload") == 0)) // Reload
            {
                if (read_config()) tsc->write_to_channel(conf["reloaded"]);
                else tsc->write_to_channel("Ошибка.");
            }
            else if (msg.find(conf["find"]) == 0) // Поиск
            {
                if (msg.find(' ') == std::string::npos) {
                    tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["findzero"]);
                }

                else {
                    std::string target = msg.substr(conf["find"].size()+1);
                    while (target [target .size() - 1] == '\n'||
                           target [target .size() - 1] == '\r') target.pop_back();
                    std::string result = search(target);
                    if (result != "")
                    {
                        tsc->write_to_channel(tsc->get_msg_nick() + ": " + search(target));
                    }
                    else tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["notfound"]);
                }
            }
            else // Общий обработчик
            {
                handled = false;
                for (auto value: conf)
                {
                    if (msg.find(value.first) != std::string::npos)
                    {
                        tsc->write_to_channel(tsc->get_msg_nick() + ", " + value.second);
                        handled = true;
                        break;
                    }
                }
                if (!handled) tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["help"]);
            }
        }

        if(tsc->to_raw) { // Все сообщения на канале
            std::string raw = tsc->get_raw();
            int res = write_log ("[" + tsc->get_raw_nick() + "] " + raw);

            if (res) { // Сообщение администратору об ошибке логирования
                std::string report = "PRIVMSG " + conf["admin"] +
                                                " Can't write to log. ";
                if (res == 1) report += "Can't open the file.";
                if (res == 2) report += "Logpath not found.";
                tsc->write(report);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char * argv[])
{
    if (argc >= 2) config_file = static_cast<std::string>(argv[1]);
    if (!read_config()) return 1;

    std::thread connection(make_tsc);
    handler();
    connection.join();

    return 2;
}
