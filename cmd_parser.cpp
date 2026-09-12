#include "cmd_parser.hpp"
#include "twitch_chat.hpp"

#include <sstream>
#include <iomanip>
#include <string>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <charconv>
#include <string_view>
#include <algorithm>
#include <lua.hpp>


Commands::Commands(const std::string& load_path)
    : load_path_(std::move(load_path)),
      lua_(luaL_newstate())
{
    luaL_openlibs(lua_);
    load();
}
Commands::~Commands()
{
    lua_close(lua_);
}


void Commands::load() {
    std::ifstream file(load_path_);
    if (!file.is_open()) {std::cerr << "Cannot load " << load_path_ << std::endl; return;}
    std::string line;
    int command_count = 0;
    while (std::getline(file, line)) {
        // TODO do better counting or smth
        if (line.empty() || line.rfind("//", 0) == 0) {continue;}
        const std::size_t main_split = line.find("^");
        if (main_split == std::string::npos) {continue;}
        line.resize(main_split);
        command_count += 1;
    }

    file.clear();
    file.seekg(0);

    // Not a fan of it. It breaks the long wait/rare commands. But we need a database to fix that.
    cooldowns.clear();
    user_cooldowns.clear();

    command_lookup.clear();
    command_names.clear();
    for (auto& [_, cmd] : commands) {
        if (cmd.lua_ref != LUA_NOREF) {luaL_unref(lua_, LUA_REGISTRYINDEX, cmd.lua_ref);}
    }
    commands.clear();

    commands.reserve(command_count);  // We know that its at least that.
    command_lookup.reserve(command_count);  // ^
    command_names.reserve(command_count);  // Could little under in edgecases
    cooldowns.reserve(command_count);  // Maybe too much


    CommandId current_id = 0;
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
        } else {command_output="";}  // aka command disabled

        auto [it, _] = commands.emplace(
            current_id,
            Command{.command = command_output,
                    .cooldown = std::chrono::seconds(5) }
        );
        Command* cmd_ptr = &it->second;
        std::string command_name;
        std::size_t start = 0;
        while (start < command_parse.size()) {
            const std::size_t end = command_parse.find('|', start);
            const std::size_t len = end == std::string::npos ? command_parse.size() - start : end - start;
            const std::string_view parse_{command_parse.data() + start, len};
            start = end == std::string::npos ? command_parse.size() : end+1;
            if (parse_.empty()) {continue;}

            if (parse_ == "LUA") {cmd_ptr->LUA=true; continue;}
            if (parse_ == "LUA_MESSAGE") {cmd_ptr->LUA_message=true; continue;}
            if (parse_ == "LUA_AUTHOR") {cmd_ptr->LUA_author=true; continue;}
            if (parse_ == "LUA_BADGES") {cmd_ptr->LUA_badges=true; continue;}
            if (parse_ == "LUA_REPLY") {cmd_ptr->LUA_reply=true; continue;}

            const std::size_t eq_split = parse_.find('=');
            if (eq_split != std::string_view::npos) {
                const std::string_view var_name = parse_.substr(0, eq_split);
                const std::string_view var_value = parse_.substr(eq_split + 1);
                if (var_name == "COOLDOWN") {
                    int value;
                    auto [ptr, ec] = std::from_chars(var_value.data(), var_value.data() + var_value.size(), value);
                    if (ec == std::errc{} && ptr == var_value.data() + var_value.size()) {
                        cmd_ptr->cooldown = std::chrono::seconds(value);
                        continue;
                    }
                }
                if (var_name == "USER_COOLDOWN") {
                    int value;
                    auto [ptr, ec] = std::from_chars(var_value.data(), var_value.data() + var_value.size(), value);
                    if (ec == std::errc{} && ptr == var_value.data() + var_value.size()) {
                        cmd_ptr->user_cooldown = std::chrono::seconds(value);
                        continue;
                    }
                }
            }

            if (command_name.empty()) {
                command_name = parse_;
                if (command_name == "_") {continue;}  // Hidden (find a better way, maybe a flag?)

                auto [cmd_lookup_it, inserted] = command_lookup.insert_or_assign(command_name, current_id);
                if (!inserted) {
                    const auto name_it = std::find(command_names.begin(), command_names.end(), command_name);
                    if (name_it != command_names.end()) { command_names.erase(name_it); }
                }
                command_names.push_back(command_name);
                continue;
            }

            const auto [_, inserted] = command_lookup.insert_or_assign(std::string(parse_), current_id);
            if (!inserted) {
                const auto name_it = std::find(command_names.begin(), command_names.end(), parse_);
                if (name_it != command_names.end()) { command_names.erase(name_it); }
            }
        }

        // compile LUA's
        if (cmd_ptr->LUA && !cmd_ptr->command.empty()) {
            const std::string code = "return " + cmd_ptr->command;
            const std::string chunk_name = "command-"+std::to_string(current_id)+"-(\""+command_name+"\")";
            if (luaL_loadbuffer(lua_, code.data(), code.size(), chunk_name.c_str())!=LUA_OK) {
                std::cerr << "Command lua compile error in " << load_path_ << ":\n" << lua_tostring(lua_, -1) << std::endl;
                lua_pop(lua_, 1);
            } else {
                cmd_ptr->lua_ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
            }
        }
        current_id++;
    }
}

void Commands::set_var(const std::string& name, const std::string& value) {
    lua_pushlstring(lua_, value.data(), value.size());
    lua_setglobal(lua_, name.c_str());
}
void Commands::set_var(const std::string& name, lua_Integer value) {
    lua_pushinteger(lua_, value);
    lua_setglobal(lua_, name.c_str());
}
void Commands::set_var(const std::string& name, bool value) {
    lua_pushboolean(lua_, value);
    lua_setglobal(lua_, name.c_str());
}
void Commands::set_field(const char* name, const std::string& value) {
    lua_pushlstring(lua_, value.data(), value.size());
    lua_setfield(lua_, -2, name);
}
void Commands::set_field(const char* name, bool value) {
    lua_pushboolean(lua_, value);
    lua_setfield(lua_, -2, name);
}
void Commands::set_field(const char* name, lua_Integer value) {
    lua_pushinteger(lua_, value);
    lua_setfield(lua_, -2, name);
}


std::string get_time(const std::string& timezone) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::chrono::zoned_time<std::chrono::system_clock::duration> zoned{timezone, now};

    std::chrono::local_time<std::chrono::system_clock::duration> local = zoned.get_local_time();
    std::chrono::local_days day = std::chrono::floor<std::chrono::days>(local);

    std::chrono::hh_mm_ss<std::chrono::system_clock::duration> time{local - day};

    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << time.hours().count() << ':' << std::setw(2) << time.minutes().count();
    return out.str();
}
int lua_get_time(lua_State* lua) {
    const char* timezone = luaL_checkstring(lua, 1);
    const std::string time = get_time(timezone);
    lua_pushlstring(lua, time.data(), time.size());
    return 1;
}

std::string Commands::check(const std::string& command, const TwitchMessage& message) {
    const auto cmd_id_it = command_lookup.find(command);
    if (cmd_id_it == command_lookup.end()) {return "";}
    const Commands::CommandId cmd_id = cmd_id_it->second;
    const auto cmd_it = commands.find(cmd_id);
    if (cmd_it == commands.end()) {
        // should not happen
        std::cerr << "Command (ID" << cmd_id << ") for '" << command << "' not found" << std::endl;
        return "";
    }
    Command* cmd = &cmd_it->second;

    // cooldown
    const auto now = std::chrono::steady_clock::now();
    auto last_used_it = cooldowns.find(cmd_id);
    if (last_used_it == cooldowns.end()) {
        cooldowns.emplace(cmd_id, now);
    } else {
        if (now - last_used_it->second < cmd->cooldown) {return "";}
        last_used_it->second = now;
    }

    if (cmd->user_cooldown != std::chrono::seconds::zero()) {
        auto user_uses_it = user_cooldowns.find(message.author.user_id);
        if (user_uses_it == user_cooldowns.end()) {
            user_uses_it = user_cooldowns.try_emplace(message.author.user_id).first;
            user_uses_it->second.emplace(cmd_id, now);
        } else {
            std::unordered_map<Commands::CommandId, std::chrono::steady_clock::time_point>& user_uses = user_uses_it->second;
            auto user_last_use_it = user_uses.find(cmd_id);
            if (user_last_use_it == user_uses.end()) {
                user_uses.emplace(cmd_id, now);
            } else {
                if (now - user_last_use_it->second < cmd->user_cooldown) {return "";}
                user_last_use_it->second = now;
            }
        }
    } 

    if (!cmd->LUA) {return cmd->command;}
    if (cmd->lua_ref == LUA_NOREF) {return "";}
    // lua varbiles

    lua_newtable(lua_);
    if (cmd->LUA_message) {
        set_field("id", message.id);
        set_field("room_id", message.room_id);
        set_field("room_name", message.room_name);
        set_field("message", message.message);
        set_field("responding", message.responding);
        set_field("timestamp", static_cast<lua_Integer>(message.timestamp));
        set_field("first_message", message.first_message);
    }
    if (cmd->LUA_author || cmd->LUA_badges) {
        lua_newtable(lua_);
        if (cmd->LUA_author) {
            set_field("login", message.author.login);
            set_field("display_name", message.author.display_name);
            set_field("user_id", message.author.user_id);
            set_field("color", message.author.color);
            set_field("returning_chatter", message.author.returning_chatter);
            set_field("sub", message.author.sub);
            set_field("vip", message.author.vip);
            set_field("mod", message.author.mod);
            set_field("turbo", message.author.turbo);
            set_field("broadcaster", message.author.broadcaster);
        }
        if (cmd->LUA_badges) {
            lua_newtable(lua_);
            for (const auto& [name, badge] : message.author.badges) {
                lua_newtable(lua_);
                set_field("version", badge.version);
                set_field("info", badge.info);
                lua_setfield(lua_, -2, name.c_str());
            }
            lua_setfield(lua_, -2, "badges");
        }
        lua_setfield(lua_, -2, "author");
    }
    if (cmd->LUA_reply) {
        lua_newtable(lua_);
        set_field("parent_message_id", message.reply_msg.parent_message_id);
        set_field("parent_user_id", message.reply_msg.parent_user_id);
        set_field("parent_user_login", message.reply_msg.parent_user_login);
        set_field("parent_display_name", message.reply_msg.parent_display_name);
        set_field("parent_message", message.reply_msg.parent_message);
        set_field("thread_parent_message_id", message.reply_msg.thread_parent_message_id);
        set_field("thread_parent_user_id", message.reply_msg.thread_parent_user_id);
        set_field("thread_parent_user_login", message.reply_msg.thread_parent_user_login);
        set_field("thread_parent_display_name", message.reply_msg.thread_parent_display_name);
        lua_setfield(lua_, -2, "reply");
    }
    lua_setglobal(lua_, "MSG");

    std::size_t random = std::hash<std::string>{}(message.id);  // pseudorandom
    set_var("RANDOM", static_cast<lua_Integer>(random));

    lua_pushcfunction(lua_, lua_get_time);
    lua_setglobal(lua_, "TIME");
    Commands::set_var("TIMESTAMP", static_cast<lua_Integer>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));

    // run code
    lua_rawgeti(lua_, LUA_REGISTRYINDEX, cmd->lua_ref);
    if (lua_pcall(lua_, 0, 1, 0) != LUA_OK) {
        std::cerr << "Command lua runtime error: " << lua_tostring(lua_, -1) << '\n';
        lua_pop(lua_, 1);
        return "";
    }

    size_t len;
    const char* result = lua_tolstring(lua_, -1, &len);
    if (!result) {
        lua_pop(lua_, 1); return "";
    }

    std::string out(result, len);
    lua_pop(lua_, 1); return out;
}

int Commands::clean_cooldowns() {
    int cleaned;
    // could also clean normal cooldowns?
    const auto now = std::chrono::steady_clock::now();

    auto user_it = user_cooldowns.begin();
    while (user_it != user_cooldowns.end()) {
        std::unordered_map<Commands::CommandId, std::chrono::steady_clock::time_point>& user_cmd_cooldowns = user_it->second;
        auto command_cooldown_it = user_cmd_cooldowns.begin();
        while (command_cooldown_it != user_cmd_cooldowns.end()) {
            auto cmd_it = commands.find(command_cooldown_it->first);
            if (cmd_it == commands.end()) {
                std::cerr << "Command (ID" << command_cooldown_it->first << ") for not found" << std::endl;
                command_cooldown_it = user_cmd_cooldowns.erase(command_cooldown_it);
                continue;
            }
            if (now - command_cooldown_it->second > cmd_it->second.user_cooldown) {
                command_cooldown_it = user_cmd_cooldowns.erase(command_cooldown_it);
                continue;
            }
            command_cooldown_it++;
        }
        if (command_cooldown_it == user_cmd_cooldowns.begin()) {
            cleaned++;
            user_it = user_cooldowns.erase(user_it);  // No longed needed.
        } else {user_it++;}
    }
    return cleaned;
}


const std::unordered_map<Commands::CommandId, Commands::Command>& Commands::get_commands() const {return commands;}
const std::vector<std::string>& Commands::get_commands_order() const {return command_names;}
