#include "cmd_parser.hpp"
#include "twitch_chat.hpp"

#include <string>
#include <fstream>
#include <unordered_map>
#include <iostream>
#include <algorithm>

Commands::Commands(const std::string& load_path)
    : load_path_(std::move(load_path))
{
    load();
}


void Commands::load() {
    std::ifstream file(load_path_);
    if (!file.is_open()) {std::cerr << "Cannot load " << load_path_ << std::endl; return;}
    std::string line;
    int command_count_ = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line.rfind("//", 0) == 0) {continue;}
        const std::size_t main_split = line.find("^");
        if (main_split == std::string::npos) {continue;}
        line.resize(main_split);
        command_count_ += std::count(line.begin(), line.end(), '|') + 1;
    }

    file.clear();
    file.seekg(0);

    commands.clear();
    command_list_.clear();
    command_list_.reserve(command_count_);

    while (std::getline(file, line)) {

        if (line.empty() || line.rfind("//", 0) == 0) {continue;}

        const std::size_t main_split = line.find("^");
        if (main_split == std::string::npos) {
            std::cerr << "Invalid line in " << load_path_ << ":\n" << line << std::endl;
            continue;
        }

        std::string command_parse = line.substr(0, main_split);
        std::string command_output;
        if (main_split+1 < line.size()) {
            command_output = line.substr(main_split+1);
        } else {command_output="";}

        while (!command_parse.empty()) {
            std::size_t command_split = command_parse.rfind("|");
            if (command_split == std::string::npos) {
                command_list_.emplace_back(command_parse);
                commands[command_list_.back()] = command_output;
                break;
            } else {
                std::string_view command_name(command_parse.data()+command_split+1, command_parse.size()-command_split-1);
                command_list_.emplace_back(command_name);
                commands[command_list_.back()] = command_output;
                command_parse.resize(command_split);
            }
        }
    }
}



std::string get_time(const std::string &timezone) {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char buf[6];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}


std::string Commands::check(const std::string& command, const TwitchMessage& message) const {
    std::unordered_map<std::string_view, std::string>::const_iterator cmd = commands.find(command);
    if (cmd == commands.end()) {return "";}

    std::string out; 
    for (std::size_t i = 0; i < cmd->second.size(); i++) {
        if (cmd->second.compare(i, 2, "${") == 0) {
            i++;
            if      (cmd->second.compare(i+1, 8+1, "NICKNAME}")==0) {out+=message.author.login;i+=8+1;}
            else if (cmd->second.compare(i+1, 7+1, "MESSAGE}" )==0) {out+=message.message;i+=7+1;}
            else if (cmd->second.compare(i+1, 9,   "TIMEZONE-" )==0) {
                const size_t start = i+1+9;
                const std::size_t found = cmd->second.find('}', start);
                if (found == std::string::npos) {
                    std::cerr << "Invalid command (timezone out of bounce); "<<cmd->second<<std::endl;
                    continue;
                }
                out+=get_time(cmd->second.substr(start, found-start));
                i=found;
            }
            else    {out += "${";}
            continue;
        }

        out += cmd->second[i];
        if (cmd->second[i] == '\\') {out += cmd->second[++i];}
    }

    return out;
}


std::vector<std::string> Commands::command_list() const {return command_list_;}
