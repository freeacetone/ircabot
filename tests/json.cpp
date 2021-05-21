#include <iostream>
#include <vector>
#include <map>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

int main()
{
    boost::property_tree::ptree pt;
    boost::property_tree::read_json("file.json", pt);

    std::map<std::string, std::string> params = { {"variable", ""}, {"double", ""} };

    boost::property_tree::ptree::const_iterator end = pt.end();

    for (boost::property_tree::ptree::const_iterator it = pt.begin(); it != end; ++it) {
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[it->first] != "")
            {
                params[it->first] = it->second.get_value<std::string>();
            }
        }
    }
        std::cout << params["variable"] << " " << params["help"];
}
