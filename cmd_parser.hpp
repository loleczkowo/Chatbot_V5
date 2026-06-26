#pragma once

#include "twitch_chat.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class Commands {
public:
    Commands(
        const std::string& load_path
    );

    void load(); // loads/reloads load_path    
    std::string check(const std::string& command, const TwitchMessage& message) const;
    std::vector<std::string> command_list() const;

private:
    const std::string load_path_;
    std::vector<std::string> command_list_;
    std::unordered_map<std::string_view, std::string> commands;
};
