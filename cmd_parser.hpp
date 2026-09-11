#pragma once

#include "twitch_chat.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <lua.hpp>


class Commands {
public:
    using CommandId = uint16_t;
    struct Command {
        std::string command;

        std::chrono::seconds cooldown;
        std::chrono::seconds user_cooldown{};

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
    const std::unordered_map<CommandId, Command>& get_commands() const;
    const std::vector<const std::string*>& get_commands_order() const;
    std::string check(const std::string& command, const TwitchMessage& message);

private:
    const std::string load_path_;
    std::unordered_map<CommandId, Command> commands;
    std::vector<const std::string*> command_names;
    std::unordered_map<std::string, CommandId> command_lookup;

    std::unordered_map<CommandId, std::chrono::steady_clock::time_point> cooldowns{};
    std::unordered_map<std::string, std::unordered_map<CommandId, std::chrono::steady_clock::time_point>> user_cooldowns{};

    void set_var(const std::string& name, const std::string& value);
    void set_var(const std::string& name, lua_Integer value);
    void set_var(const std::string& name, bool value);
    void set_field(const char* name, const std::string& value);
    void set_field(const char* name, bool value);
    void set_field(const char* name, lua_Integer value);
    lua_State* lua_;    
};
