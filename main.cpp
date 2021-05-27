#include <iostream>
#include <fstream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <vector>
#include <map>
#include <thread>
#include <boost/filesystem.hpp>
#include "tcpsyncclient.h"

boost::asio::io_service service;
std::string config_file = "ircbot.json";
TcpSyncClient * volatile tsc = nullptr;
bool tsc_created = false;

#ifdef WIN32
    std::string slash = "\\";
#else
    std::string slash = "/";
#endif

void log(std::string text)
{
    std::cout << "[DBG] " << text << std::endl;
}

std::map<std::string, std::string> conf =
{
    { "admin"   , "acetone"   }, // никнейм админа
    { "error"   , "error"     }, // сообщение об ошибке
    { "success" , "success"   }, // сообщение об успехе
    { "logpath" , ""          }, // директория с логами
    { "find"    , "search"    }, // команда поиска
    { "notfound", "not found" }, // поиск не увенчался успехом
    { "findzero", "what?.."   }, // команда поиска без параметров
    { "links"   , ""          }, // ссылки на лог (в конце выдачи в ЛС)
    { "trylater", "try later" }  // "перегрузка, попробуйте позже"
};

std::mutex mtx;
std::vector<std::string> vectorStringsTransit;
std::string              stringNickTransit;
constexpr unsigned       sendVectorToUser_MAXIMUM = 3;
unsigned                 sendVectorToUser_COUNTER = 0;
void sendVectorToUser()
{
    log ("sendVectorToUser+ " + std::to_string(++sendVectorToUser_COUNTER));

    mtx.lock();
    std::vector<std::string> messages = vectorStringsTransit;
    vectorStringsTransit.clear();
    std::string nick = stringNickTransit;
    stringNickTransit.clear();
    mtx.unlock();  

    int messageCounter = 0;
    bool stopped = false;
    for (auto str: messages)
    {
        if (tsc->have_pm_from_user(nick)) { // Ник появился в стопе
            stopped = true;
            break;
        }

        if (messageCounter++ < 20) {
            tsc->write_to_user(nick, str);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else {
            messageCounter = 0;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            tsc->write_to_user(nick, str);
        }
    }
    tsc->write_to_user(nick, stopped ? "*** STOP ***" : "*** END ***");
    tsc->write_to_user(nick, conf["links"]);

    log ("sendVectorToUser+ " + std::to_string(--sendVectorToUser_COUNTER));
}

std::vector<std::string> search_detail(std::string date, std::string text)
{
    std::string year  = date.substr(0, 4);                              // YYYY
    std::string month = date.substr(year.size()+1, 2);                  // MM
    std::string day   = date.substr(year.size() + month.size() + 2, 2); // DD
    std::vector<std::string> result;

    log ("search_detail() " + year + "-" + month + "-" + day + " '" + text + "'");
    std::regex regex;
    if (text != "") // Нужен не весь лог, а конкретные сообщения
    {
        std::regex r(".*" + text + ".*", std::regex_constants::basic | std::regex_constants::icase);
        regex = r;
    }

    std::string path = conf["logpath"] + slash + year + slash + month + slash + day + ".txt";
    if (! boost::filesystem::exists(path)) return result;

    std::ifstream log(path);
    std::string buffer;
    while(getline(log, buffer))
    {
        if (text == "") result.push_back(buffer);
        else if (std::regex_match(buffer, regex)) result.push_back(buffer);
    }
    log.close();

    return result;
}

std::string search(std::string text)
{
    constexpr int maxSize = 10;
    std::string values; // Строка для возврата

    uint64_t success = 0; // Счетчик уникальных вхождений
    uint64_t total = 0;   // Общий счетчик вхождений

    std::map<std::string, uint64_t> stats; // Линковка даты и ее счетчика
    std::vector<std::string> matches;      // Значения, компонуемые в итоговую строку

    std::regex regex(".*" + text + ".*", std::regex_constants::basic | std::regex_constants::icase);

    if (! boost::filesystem::exists(conf["logpath"])) return values;

    boost::filesystem::recursive_directory_iterator dir(conf["logpath"]), end;
    for (; dir != end; ++dir)
    {
        if (boost::filesystem::is_directory(dir->path())) continue;

        std::string buffer;
        std::ifstream log(dir->path().c_str());
        while(getline(log, buffer))
        {
            if (std::regex_match(buffer, regex))
            {
                ++total;
                bool first = true;
                std::string date = buffer.substr(0, buffer.find(' '));
                stats[date] += 1; // Счетчик конкретной даты

                for (auto entry: matches)
                {
                    if (entry.find(date) != std::string::npos)
                    {
                        first = false;
                    }
                }
                if (first)
                {
                    matches.push_back(date);
                    ++success;
                }
            }
        }

        log.close();
    }

    if (matches.size() > 0)
    {
        for (auto it = matches.begin(), end = matches.end(); it != end; ++it)
        {
            *it += " (" + std::to_string(stats[*it]) + ")";
        }

        std::sort(matches.begin(), matches.end());
        values += "[" + text + ": " + std::to_string(total) + "] ";

        for (int i = matches.size()-1, count = 0; i >= 0 && count < maxSize ; --i, ++count)
        { // Компоновка выходной строки
            if (values.find('-') != std::string::npos) values += ", ";
            values += matches[i];
        }

        for (auto it = values.begin(), end = values.end(); it != end; ++it)
        { // Замена тире на слеш
            if (*it == '-') *it = '/';
        }

        if (success > maxSize) values += "...";
        else values += ".";
    }
    return values;
}

bool read_config()
{
    if (!boost::filesystem::exists(config_file)) {
        log ("Config not found");
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

    out << year << "-" << month << "-" << day << " " << msg << std::endl;
    out.close();
    return 0;
}

void usage(std::string path)
{
    std::cout << "Usage: " << path << " <path/to/config.json>" << std::endl;
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
    if (!tsc_created) std::this_thread::sleep_for(std::chrono::milliseconds(600));
    bool handled = false;

    while (true)
    {
        if (tsc->to_read) { // Есть сообщения, адресованные боту
            std::string msg = tsc->get_msg();

            if (tsc->get_msg_nick() == conf["admin"] && (msg.find("reload") == 0)) //// Reload
            {
                if (read_config()) tsc->write_to_channel(conf["success"]);
                else tsc->write_to_channel("Ошибка.");
            }

            else if (msg.find(conf["find"]) == 0) //// Поиск
            {
                std::regex date_check(conf["find"] + " [0-9]{4}.[0-9]{2}.[0-9]{2}.*", std::regex_constants::egrep);

                if (msg.find('*') != std::string::npos) { // Защита от хитрой регулярки
                    tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["error"]);
                }
                else if (msg.find(' ') == std::string::npos) {
                    tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["findzero"]);
                }

                else if (std::regex_match(msg, date_check)) { //// Запрос по дате
                    std::string pattern;
                    std::string date = msg.substr(conf["find"].size()+1, 10); // 10 == date size

                    if (msg.substr(conf["find"].size()+11).find(' ') != std::string::npos)
                    {// Поиск по слову
                        pattern = msg.substr(conf["find"].size() + date.size() + 2);
                    }

                    std::vector<std::string> result = search_detail(date, pattern);

                    if (! result.empty())
                    {
                        std::string nick = tsc->get_msg_nick();
                        std::string header = date;
                        if (pattern != "") header += " # " + pattern;

                        if (sendVectorToUser_COUNTER < sendVectorToUser_MAXIMUM)
                        {
                            tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["success"]);
                            tsc->write_to_user(nick, "[" + header + "]");
                            mtx.lock();
                            vectorStringsTransit = result;
                            stringNickTransit = nick;
                            mtx.unlock();
                            std::thread (sendVectorToUser).detach();
                        }
                        else {
                            tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["trylater"]);
                        }
                    }
                    else tsc->write_to_channel(tsc->get_msg_nick() + ", " + conf["error"]);
                }

                else { //// Поиск с минимальной выдачей
                    std::string target = msg.substr(conf["find"].size()+1);
                    std::string result = search(target);
                    if (result != "")
                    {
                        tsc->write_to_channel(search(target));
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

        if (tsc->to_raw) { // Все сообщения на канале
            std::string raw = tsc->get_raw();
            int res = write_log ("[" + tsc->get_raw_nick() + "] " + raw);

            if (res) { // Сообщение администратору об ошибке логирования
                std::string report = "Can't write to log. ";
                if (res == 1) report += "Can't open the file.";
                if (res == 2) report += "Logpath not found.";
                tsc->write_to_user(conf["admin"], report);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char * argv[])
{
    if (argc > 1) config_file = static_cast<std::string>(argv[1]);
    else {
        usage(static_cast<std::string>(argv[0]));
        return 1;
    }
    if (!read_config()) return 2;

    std::thread connection(make_tsc);
    handler();
    connection.join();

    return 3;
}
