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
    // int alias_count = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line.rfind("//", 0) == 0) {continue;}
        const std::size_t main_split = line.find("^");
        if (main_split == std::string::npos) {continue;}
        line.resize(main_split);
        command_count += 1;
        // alias_count += std::count(line.begin(), line.end(), '|');  // DOES NOT COUNT ALIASES PROPERLY
    }

    file.clear();
    file.seekg(0);

    aliases.clear();
    commands_order.clear();
    for (auto& [_, cmd] : commands) {
        if (cmd.lua_ref != LUA_NOREF) {luaL_unref(lua_, LUA_REGISTRYINDEX, cmd.lua_ref);}
    }
    commands.clear();
    // aliases.reserve(alias_count);
    commands.reserve(command_count);


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

        // I gave up, this is very unsafe(?) but works ig?
        Command cmd{
            .command = command_output,
            .cooldown = std::chrono::seconds(5)
        };
        Command* cmd_ptr = &cmd;
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
            }

            if (command_name.empty()) {
                auto [it, _] = commands.emplace(parse_, std::move(cmd));
                cmd_ptr = &it->second;
                commands_order.push_back(&it->first);
                command_name = parse_;
                continue;
            }
            aliases.emplace(parse_, cmd_ptr);
        }

        // compile LUA's
        if (cmd_ptr->LUA && !cmd_ptr->command.empty()) {
            const std::string code = "return " + cmd_ptr->command;
            const std::string chunk_name = "command-"+command_name;
            if (luaL_loadbuffer(lua_, code.data(), code.size(), chunk_name.c_str())!=LUA_OK) {
                std::cerr << "Command lua compile error in " << load_path_ << ":\n" << lua_tostring(lua_, -1) << std::endl;
                lua_pop(lua_, 1);
            } else {
                cmd_ptr->lua_ref = luaL_ref(lua_, LUA_REGISTRYINDEX);
            }
        }
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
    auto cmd_it = commands.find(command);
    Command* cmd;
    if (cmd_it == commands.end()) {
        auto cmd_it_alias = aliases.find(command);
        if (cmd_it_alias == aliases.end()) {return "";}
        cmd=cmd_it_alias->second;
    } else {cmd=&cmd_it->second;}

    // cooldown
    const auto now = std::chrono::steady_clock::now();
    if (now - cmd->last_used < cmd->cooldown) {return "";}
    cmd->last_used = now;

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


const std::unordered_map<std::string, Commands::Command>& Commands::get_commands() const {return commands;}
const std::vector<const std::string*>& Commands::get_commands_order() const {return commands_order;}
