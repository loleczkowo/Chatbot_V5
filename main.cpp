#include "types.hpp"
#include "twitch_auth.hpp"
#include "twitch_chat.hpp"
#include "twitch_api.hpp"
#include "lua_apis.hpp"
#include "twitch_eventsub.hpp"
#include "cmd_parser.hpp"

#include <json/json.h>

#include <iostream>
#include <fstream>
#include <string>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>


std::vector<std::string> channels;
std::unordered_set<std::string> chatbots;
std::unordered_map<std::string, ChannelContext> channels_contexts;
std::unordered_map<std::string, Commands> channels_commands;


void debug_on_msg(const TwitchMessage& message) {
    if (message.responding) {
        std::cout << "  > "
            << message.reply_msg.parent_display_name << ": "
            << message.reply_msg.parent_message << std::endl;
    }
    std::cout << message.author.display_name << ": "
              << message.message << std::endl;
    // for (const auto& badge : message.author.badges) {std::cout << badge.first << " ";} std::cout << std::endl;
}

void stream_online(const Json::Value& payload) {
    const std::string type = payload["subscription"]["type"].asString();
    const std::string login = payload["event"]["broadcaster_user_login"].asString();
    if (type == "stream.online") {
        channels_contexts[login].online = true;
    } else if (type == "stream.offline") {
        channels_contexts[login].online = false;
    }
}

void chat_commands(
    const TwitchMessage& message, TwitchApi& api
)
{
    // prevent bots from using commands
    if (chatbots.find(message.author.login) != chatbots.end()) return;
    if (message.author.badges.find("bot") != message.author.badges.end()) return;

    auto it = channels_commands.find(message.room_name);
    if (it == channels_commands.end()) return;  // if no config file then all cmds disabled
    Commands& commands = it->second; 
    auto context_it = channels_contexts.find(message.room_name);
    ChannelContext context{};
    if (context_it != channels_contexts.end()) context = context_it->second; 

    std::string message_str = message.message;
    if (message.responding) {message_str.erase(0, message.reply_msg.parent_user_login.length()+2);}

    // TODO rework that. It wont work for some commands.
    const std::size_t first_space_ = message_str.find(' ');
    std::string cmd_;
    std::string cut_msg;
    if (first_space_ == std::string::npos) {
        cmd_ = message_str;
        cut_msg = "";
    }
    else {
        cmd_ = message_str.substr(0, first_space_);
        cut_msg = message_str.substr(first_space_+1);
    }

    const std::string command_return = commands.check(cmd_, message, cut_msg, context);
    if (command_return == " ") {return;}
    
    const std::string respond_to = message.responding ? message.reply_msg.parent_message_id : message.id;
    
    if (!command_return.empty()) {
        Json::Value api_result_ = api.send_message(message.room_id, command_return, respond_to);
        if (api_result_["data"][0]["is_send"].asBool()) {
            std::cerr << api_result_ << std::endl;
        }
        return;
    }
    if (message.message == "!commands") {
        const std::vector<std::string> cmds_list = commands.get_commands_order();
        std::string respond = "";
        for (const std::string& cmd_name : cmds_list) {
            respond += cmd_name + " ";
        }
        api.send_message(message.room_id, respond, respond_to);
        return;
    }
}

void input_loop(std::atomic<bool>& running, std::string& command) {
    std::string command_;
    while (std::getline(std::cin, command_)) {
        if (command_ == "quit" || command_ == "exit") {
            std::cout << "Exiting program . . ." << std::endl;
            running = false;
            break;
        } else {
            command = std::move(command_);
        }
        command_.clear();
    }
}

int main()
{
    const CURLcode curl_code = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_code != CURLE_OK) {
        std::cerr << "curl_global_init failed: " << curl_easy_strerror(curl_code) << std::endl;
        return 1;
    }

    std::cout << "Reading config" << std::endl;
    std::ifstream secret_file("client_secret");
    std::string client_id;
    std::string client_secret;
    std::string line;
    int i = 0;
    // cant wait to look at this in few years and laugh how STUPID i was lolz
    while (std::getline(secret_file, line)) {
        switch (i) {
            case 0:
                client_id = line;
                break;
            case 1:
                client_secret = line;
                break;
            default:
                std::cout << "WARNING; Unknown line " << line << std::endl;
                break;
        }
        i++;
    }

    std::ifstream config_file("config");
    std::string bot_nickname;
    std::string oauth_port_string;
    int oauth_port;
    std::string oauth_redirect;
    while (std::getline(config_file, line)) {
        const std::size_t equal_pos = line.find("=");
        if (equal_pos == std::string::npos) {continue;}
        const std::string conf_name = line.substr(0, equal_pos);
        std::string conf_value = line.substr(equal_pos+1);
        if (conf_name=="nickname") {bot_nickname=conf_value;}
        else if (conf_name=="oauth_port") {oauth_port_string=conf_value;}
        else if (conf_name=="oauth_redirect") {oauth_redirect=conf_value;}
        else if (conf_name=="channels") {
            while (true) { 
                const std::size_t listcut_ = conf_value.find(",");
                if (listcut_ == std::string::npos) {channels.push_back(conf_value); break;}
                channels.push_back(conf_value.substr(0, listcut_));
                conf_value = conf_value.substr(listcut_+1);
            }
        }
        else if (conf_name=="chatbots") {
            while (true) { 
                const std::size_t listcut_ = conf_value.find(",");
                if (listcut_ == std::string::npos) {chatbots.emplace(conf_value); break;}
                chatbots.emplace(conf_value.substr(0, listcut_));
                conf_value = conf_value.substr(listcut_+1);
            }
        }
    }
    bool missing = false;
    if (bot_nickname.empty()) {
        std::cerr << "Missing 'bot_nickname' in config file" << std::endl; missing = true;
    }
    if (oauth_port_string.empty()) {
        std::cerr << "Missing 'oauth_port' in config file" << std::endl; missing = true;
    } else {
        try {oauth_port = std::stoi(oauth_port_string); if (oauth_port<0 || oauth_port>65535) {std::cerr << "'oauth_port' is out of range for a port" << std::endl; missing=true;}}
        catch (const std::invalid_argument&) {std::cerr << "Cannot convert 'oauth_port' to an number" << std::endl; missing=true;}
        catch (const std::out_of_range&) {std::cerr << "Cannot convert 'oauth_port' to a number (Too large)" << std::endl; missing=true;}
    }
    if (channels.empty()) {
        std::cout << "WARNING: no 'channels' have been given in the config file" << std::endl;
    }
    if (missing) {return 1;}

    TwitchAuth auth(
        client_id,
        client_secret,
        oauth_port,
        oauth_redirect
    );
    std::cout << "loading&checking oauth" << std::endl;
    auth.load_oauth();
    auth.oauth_check();
    const time_t now = std::time(nullptr);
    int64_t last_oauth_check = now;
    int64_t last_cmd_clear_cooldowns = now;

    std::cout << "starting oauth2 server" << std::endl;
    auth.start_oauth2_server();
    
    std::cout << "fetching app token..." << std::endl;
    std::string token = auth.get_token();
    std::cout << "fetching bot token..." << std::endl;
    TwitchApi twitch_api (auth, bot_nickname);

    std::cout << "Fetching channel contexts." << std::endl;
    for (const std::string& channel : channels) {
        channels_contexts.try_emplace(channel);
    }
    Json::Value streams = twitch_api.get_streams(channels);
    for (auto stream : streams) {
        channels_contexts[stream["user_login"].asString()].online = stream["type"].asString() == "live";
    }
    std::cout << "Loading commands for; ";
    for (const std::string& channel : channels) {
        std::cout << channel << " " << std::flush;
        const std::string commands_path = "commands/"+channel+".txt";
        struct stat buffer;
        if (stat(commands_path.c_str(), &buffer) != 0) {continue;}  // TODO only works for linux!
        Commands& commands = channels_commands.try_emplace(channel, commands_path).first->second;
        commands.add_lib("twitch_api", &twitch_api, LuaTwitchApi::functions, LuaTwitchApi::func_count);
    } std::cout << std::endl;

    const std::string bot_id = twitch_api.get_id(bot_nickname, true);
    bool bot_token_exists = false;
    bool bot_token_has_scopes = false;
    {
        TwitchLockedOAuth bot_oauth = auth.get_oauth(bot_id);
        if (bot_oauth!=nullptr) {
            bot_token_exists = true;
            bot_token_has_scopes = bot_oauth->check_scopes({"chat:read"});
        }
    }
    std::cout << "Tokens acquired." << std::endl;
    std::cout << "OAUTH2 links;" << std::endl <<
        "  bot account; " << auth.oauth2_botauth() << std::endl <<
        "  broadcaster; " << auth.oauth2_broadcasterauth() << std::endl;

    bool chat_enabled = false;
    std::optional<TwitchChat> twitch_chat;
    if (!bot_token_exists) {
        std::cerr << "Failed to get bot's oauth token. Add it and restart the program." << std::endl;
    } else if (!bot_token_has_scopes) {
        std::cerr << "Missing `chat:read` from bot's oauth token. Re-auth the token and restart the program." << std::endl;
    } else {
        chat_enabled = true;
        std::cout << "connecting twitch chat" << std::endl;
        twitch_chat.emplace(auth, bot_nickname, bot_id, channels);       
        twitch_chat->on_message(debug_on_msg);
        twitch_chat->on_message([&twitch_api](const TwitchMessage& message) {chat_commands(message, twitch_api);});
        try {
            if (!twitch_chat->connect()) {
                std::cerr << "Failed to connect to twitch" << std::endl; return 2;
            }
        } catch (const boost::system::system_error& error) {
            std::cerr << "Failed to connect chat; " << error.what() << std::endl;
        }
    }
    std::cout << "Connecting to twitch EventSub" << std::endl;
    std::unique_ptr<TwitchEventSub> twitch_event_sub = std::make_unique<TwitchEventSub>();
    twitch_event_sub->connect();
    twitch_event_sub->on_message(stream_online);
    bool subbed_events = false;
    // std::cout << twitch_api.test() << std::endl;
    std::atomic<bool> running = true;
    std::string command;
    std::thread input_thread(input_loop, std::ref(running), std::ref(command));
    std::cout << "starting main loop" << std::endl;
    while (running) {
        sleep(1);
        if (!command.empty()) {

            //if (command.rfind("send", 0) == 0) {
            //    const std::string send_message = command.substr(5);
            //    std::cout << twitch_api.send_message(twitch_api.get_id(channel, true), send_message) << std::endl;
            //} else
            if (command == "reload_cmds") {
                std::cout << "Reloading commands" << std::endl;
                for (auto& [_, commands] : channels_commands) {
                    commands.load();
                }
            }
            command.clear();
        }

        if (chat_enabled && !twitch_chat->is_connected()) {
            std::cout << "Recconecting chat" << std::endl;
            try {
                if (twitch_chat->connect()) {
                    std::cout << "Successfully recconected chat." << std::endl;
                } else {
                    std::cerr << "Failed to recconect chat" << std::endl;
                    sleep(10);
                }
            } catch (const boost::system::system_error& error) {
                std::cerr << "Failed to recconect chat; " << error.what() << std::endl;
                sleep(10);
            }
        }
        if (!auth.is_oauth2_server_alive()) {
            std::cerr << "Restarting oauth2 server (died)" << std::endl;
            auth.start_oauth2_server();
        }
        if (!twitch_event_sub->swap.empty()) {
            std::cout << "Swapping eventsub server to " << twitch_event_sub->swap << std::endl;
            std::unique_ptr<TwitchEventSub> next = std::make_unique<TwitchEventSub>(twitch_event_sub->callbacks(), twitch_event_sub->swap);
            next->connect();
            int slept = 0;
            while (!next->welcomed()) {
                sleep(1);
                slept++;
                if (slept >= 35) {
                    std::cout << "Swapping eventsub; failed swap; timeouted." << std::endl;
                    twitch_event_sub->swap.clear();
                    break;
                }
            }
            if (slept < 35) {
                std::cout << "Swapping eventsub; disconnecting old one." << std::endl;
                twitch_event_sub->disconnect();
                twitch_event_sub = std::move(next);
                subbed_events = false;
                std::cout << "Swapping eventsub; successful." << std::endl;
            }
        }
        else if (!twitch_event_sub->is_connected()) {
            std::cout << "Recconecting eventsub" << std::endl;
            twitch_event_sub->connect();
            subbed_events = false;
        }
        if (!subbed_events && twitch_event_sub->welcomed()) {
            std::cout << "Subscribing Eventsubs" << std::endl;
            const std::string& session_id = twitch_event_sub->connection_id();
            for (const std::string& channel : channels) {
                const std::string id = twitch_api.get_id(channel);
                twitch_api.eventsub_subscribe(
                    "stream.online", "1", "{\"broadcaster_user_id\":\""+id+"\"}",
                    session_id, id, {"NO"}, {}
                );
                twitch_api.eventsub_subscribe(
                    "stream.offline", "1", "{\"broadcaster_user_id\":\""+id+"\"}",
                    session_id, id, {"NO"}, {}
                );
            }
            subbed_events = true;
        }

        const time_t now = std::time(nullptr);
        if (now - last_oauth_check > 15*60) {
            std::cout << "OAuth check ..." << std::flush;
            auth.oauth_check();
            std::cout << " done!" << std::endl;
            last_oauth_check = now;
        }

        if (now - last_cmd_clear_cooldowns > 15*60) {
            std::cout << "Clearing commands cooldowns for " << std::flush;
            for (auto& [chan_name, commands] : channels_commands) {
                std::cout << chan_name << std::flush;
                std::cout << "(" << commands.clean_cooldowns() << ") " << std::flush;
            }
            std::cout << std::endl;
            last_cmd_clear_cooldowns = now;
        }
    }
    input_thread.join();
    if (chat_enabled) {
        std::cout << "disconecting twitch chat" << std::endl;
        twitch_chat->disconnect();
    }
    std::cout << "stopping twitch eventsub" << std::endl;
    twitch_event_sub->disconnect();
    std::cout << "stopping oauth2 server" << std::endl;
    auth.stop_oauth2_server();
    return 0;
}
