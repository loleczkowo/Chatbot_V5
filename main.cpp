#include "twitch_auth.hpp"
#include "twitch_chat.hpp"
#include "twitch_api.hpp"
#include "cmd_parser.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <optional>
#include <unordered_set>


void debug_on_msg(const TwitchMessage& message) {
    if (message.responding) {
        std::cout << "  > "
            << message.reply_msg.parent_display_name << ": "
            << message.reply_msg.parent_message << std::endl;
    }
    std::cout << message.author.display_name << ": "
              << message.message << std::endl;
    // for (const auto& badge : message.author.badges) {std::cout << badge.first << " ";}
    // std::cout << std::endl;

    // std::cout << message.id << std::endl;
}

void chat_commands(
    const TwitchMessage& message, TwitchApi& api,
    const Commands& commands,
    const std::unordered_set<std::string>& chatbots
)
{
    const std::size_t first_space_ = message.message.find(' ');
    std::string cmd_;
    if (first_space_ == std::string::npos) {cmd_=message.message;}
    else {cmd_=message.message.substr(0, first_space_);}

    const std::string command_return = commands.check(cmd_, message);
    if (command_return == " ") {return;}
    // I allowed echo to be recursive because its cool :)
    if (command_return.empty() && cmd_=="!echo" && first_space_ != std::string::npos) {
        api.send_message(message.room_id, message.message.substr(first_space_+1), message.id);
        return;
    }
    
    // prevent bots from using commands
    if (chatbots.find(message.author.login) != chatbots.end()) {return;}
    if (message.author.badges.find("bot") != message.author.badges.end()) {return;}
    
    if (!command_return.empty()) {
        Json::Value api_result_ = api.send_message(message.room_id, command_return, message.id);
        if (api_result_["data"][0]["is_send"].asBool()) {
            std::cerr << api_result_ << std::endl;
        }
        return;
    }
    if (message.message == "!commands") {
        const std::vector<std::string> cmds_list = commands.command_list();
        std::string respond = "";
        for (const std::string& cmd_fromlist : cmds_list) {
            respond += cmd_fromlist + " ";
        }
        api.send_message(message.room_id, respond, message.id);
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
    std::string channel;
    std::unordered_set<std::string> chatbots;
    while (std::getline(config_file, line)) {
        const std::size_t equal_pos = line.find("=");
        if (equal_pos == std::string::npos) {continue;}
        const std::string conf_name = line.substr(0, equal_pos);
        std::string conf_value = line.substr(equal_pos+1);
        if (conf_name=="nickname") {bot_nickname=conf_value;}
        else if (conf_name=="channel") {channel=conf_value;}
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
    } if (channel.empty()) {
        std::cerr << "Missing 'channel' in config file" << std::endl; missing = true;
    }
    if (missing) {return 1;}

    std::cout << "Loading command" << std::endl;
    Commands commands("commands/"+channel+".txt");

    TwitchAuth auth(
        client_id,
        client_secret
    );
    std::cout << "loading&checking oauth" << std::endl;
    auth.load_oauth();
    auth.oauth_check();
    int64_t last_oauth_check = std::time(nullptr);
    std::cout << "starting oauth2 server" << std::endl;
    auth.start_oauth2_server();
    
    std::cout << "fetching app token..." << std::endl;
    std::string token = auth.get_token();
    std::cout << "fetching bot token..." << std::endl;
    TwitchApi twitch_api (auth, bot_nickname);
    TwitchOAuth* bot_oauth = auth.get_oauth(twitch_api.get_id(bot_nickname, true));
    std::cout << "Tokens acquired." << std::endl;
    std::cout << "OAUTH2 links;" << std::endl <<
        "  bot account; " << auth.oauth2_botauth() << std::endl <<
        "  broadcaster; " << auth.oauth2_broadcasterauth() << std::endl;

    bool chat_enabled = false;
    std::optional<TwitchChat> twitch_chat;
    if (bot_oauth == nullptr) {
        std::cerr << "Failed to get bot's oauth token. Add it and restart the program";
    } else if (!bot_oauth->check_scopes({"chat:read"})) {
        std::cerr << "Missing `chat:read` from bot's oauth token. Re-auth the token and restart the program.";
    } else {
        chat_enabled = true;
        twitch_chat.emplace(*bot_oauth, client_id, client_secret, bot_nickname, channel);       
        std::cout << "connecting twitch chat" << std::endl;
        twitch_chat->on_message(debug_on_msg);
        twitch_chat->on_message(
            [&twitch_api, &chatbots, &commands](const TwitchMessage& message) {chat_commands(message, twitch_api, commands, chatbots);}
        );
        try {
            if (!twitch_chat->connect()) {
                std::cerr << "Failed to connect to twitch" << std::endl; return 2;
            }
        } catch (const boost::system::system_error& error) {
            std::cerr << "Failed to connect chat; " << error.what() << std::endl;
        }
    }
    // std::cout << twitch_api.test() << std::endl;
    std::atomic<bool> running = true;
    std::string command;
    std::thread input_thread(input_loop, std::ref(running), std::ref(command));
    std::cout << "starting main loop" << std::endl;
    while (running) {
        sleep(1);
        if (!command.empty()) {
            if (command.rfind("send", 0) == 0) {
                const std::string send_message = command.substr(5);
                std::cout << twitch_api.send_message(twitch_api.get_id(channel, true), send_message) << std::endl;
            } else if (command == "reload_cmds") {
                std::cout << "Reloading commands" << std::endl;
                commands.load();
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

        if (last_oauth_check+15*60 < std::time(nullptr)) {
            std::cout << "OAuth check" << std::endl;
            auth.oauth_check();
            last_oauth_check = std::time(nullptr);
        }
    }
    input_thread.join();
    if (chat_enabled) {
        std::cout << "disconecting twitch chat" << std::endl;
        twitch_chat->disconnect();
    }
    std::cout << "stopping oauth2 server" << std::endl;
    auth.stop_oauth2_server();
    return 0;
}