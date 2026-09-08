#pragma once

#include "twitch_chat.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <lua.hpp>


class Commands {
public:
    struct Command {
        std::string command;

        std::chrono::seconds cooldown;
        std::chrono::steady_clock::time_point last_used{};

        bool LUA{};
        bool LUA_message{};
        bool LUA_author{};
        bool LUA_badges{};
        bool LUA_reply{};

        int lua_ref = LUA_NOREF;
    };

    Commands(const std::string& load_path);
    ~Commands();

    Commands(const Commands&) = delete;
    Commands& operator=(const Commands&) = delete;

    void load(); // loads/reloads load_path    
    const std::unordered_map<std::string, Command>& get_commands() const;
    const std::vector<const std::string*>& get_commands_order() const;
    std::string check(const std::string& command, const TwitchMessage& message);

private:
    const std::string load_path_;
    std::unordered_map<std::string, Command> commands;
    std::vector<const std::string*> commands_order;
    std::unordered_map<std::string, Command*> aliases;  // unsafe?

    void set_var(const std::string& name, const std::string& value);
    void set_var(const std::string& name, lua_Integer value);
    void set_var(const std::string& name, bool value);
    void set_field(const char* name, const std::string& value);
    void set_field(const char* name, bool value);
    void set_field(const char* name, lua_Integer value);
    lua_State* lua_;    
};
