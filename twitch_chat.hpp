#pragma once

#include "twitch_auth.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
 
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>

struct TwitchBadgeInfo {
    std::string version;
    std::string info = "";
};

struct TwitchAuthor {
    std::string login;
    std::string display_name = "";
    std::string user_id;
    std::string color;

    // bool is_follower = false;
    bool returning_chatter = false;
    bool sub = false;
    bool vip = false;
    bool mod = false;
    bool turbo = false;
    bool broadcaster = false;
    std::unordered_map<std::string, TwitchBadgeInfo> badges;
};
struct TwitchReplyInfo {
    std::string parent_message_id;
    std::string parent_user_id;
    std::string parent_user_login;
    std::string parent_display_name;
    std::string parent_message;
    std::string thread_parent_message_id;
    std::string thread_parent_user_id;
    std::string thread_parent_user_login;
    std::string thread_parent_display_name;
};
struct TwitchMessage {
    std::string id;
    std::string room_id;
    std::string room_name;
    std::string message;
    TwitchReplyInfo reply_msg;
    bool responding;
    TwitchAuthor author;
    long long timestamp;
    bool first_message = false;
};


class TwitchChat {

public:
    using MessageCallback = std::function<void(const TwitchMessage&)>;
    TwitchChat(
        TwitchOAuth& oauth,
        std::string client_id,
        std::string client_secret,
        std::string bot_nickname,
        std::string channel
    );

    bool connect();
    void disconnect();
    void on_message(MessageCallback on_message);
    bool is_connected() const;

private:
    TwitchOAuth& oauth_;
    const std::string client_id_;
    const std::string client_secret_;
    const std::string bot_nickname_;
    const std::string channel_;

    void send_raw(const std::string& line);
    bool raw_connect();
    void disconnect_socket();
    std::string read_line();

    std::vector<MessageCallback> callbacks_;
    std::thread listen_thread_;
    void read_loop();
    TwitchMessage parse_message(const std::string& line) const;
    void parse_raw_msg(const std::string& raw_msg, std::string& result) const;

    bool is_privmsg(const std::string& line) const;
    
    bool connected_ = false;  // what we want to have
    bool socket_connected_ = false;  // current state of socket
    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    std::unique_ptr<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>
    > socket_;
    boost::asio::streambuf read_buffer_;
};
