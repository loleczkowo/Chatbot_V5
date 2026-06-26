#include "twitch_chat.hpp"
#include "twitch_auth.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

#include <iostream>
#include <utility>



TwitchChat::TwitchChat(
    TwitchOAuth& oauth,
    std::string client_id,
    std::string client_secret,
    std::string bot_nickname,
    std::string channel
)
    : oauth_(oauth),
      client_id_(std::move(client_id)),
      client_secret_(std::move(client_secret)),
      bot_nickname_(std::move(bot_nickname)),
      channel_(std::move(channel)),
      ssl_context_(boost::asio::ssl::context::tlsv12_client)
{
}


bool TwitchChat::connect()
{
    if (listen_thread_.joinable() && listen_thread_.get_id() != std::this_thread::get_id()) {
        listen_thread_.join();
    }

    // Better to make a new socket each connection.
    socket_ = std::make_unique<
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>
    >(io_context_, ssl_context_);

    if (!raw_connect()) {
        std::cerr << "error while connecting chat, refreshing token and retrying" << std::endl;
        oauth_.refresh(client_id_, client_secret_);
        if (!raw_connect()) {
            std::cerr << "cannot connect to chat o7" << std::endl;
            return false;
        }
    }
    connected_ = true;
    socket_connected_ = true;
    listen_thread_ = std::thread(
        &TwitchChat::read_loop,
        this
    );
    return true;
}

bool TwitchChat::raw_connect()
{
    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve("irc.chat.twitch.tv", "6697");
    boost::asio::connect(socket_->lowest_layer(), endpoints);
    socket_->handshake(boost::asio::ssl::stream_base::client);

    send_raw("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
    send_raw("PASS oauth:" + oauth_.get_token() + "\r\n");
    send_raw("NICK " + bot_nickname_ + "\r\n");
    send_raw("JOIN #" + channel_ + "\r\n");    
    while (true) {
        std::string line = read_line();
        if (line.find("Login authentication failed") != std::string::npos) {
            return false;
        }
        if (line.find("Login unsuccessful") != std::string::npos) {
            return false;
        }
        if (line.find(" 001 ") != std::string::npos) {
            return true;
        }
        if (line.rfind("PING", 0) == 0) {
            send_raw("PONG :tmi.twitch.tv\r\n");
            continue;
        }
        // std::cout << "chat|raw-connect unknown line; " << line << std::endl;
    }

}

void TwitchChat::disconnect()
{
    connected_ = false;
    boost::system::error_code shutdown_error;
    shutdown_error = socket_->shutdown(shutdown_error);
    if (shutdown_error) {
        std::cerr << "TLS shutdown error: " << shutdown_error.message() << std::endl;
    }
    disconnect_socket();
    if (listen_thread_.joinable()&&listen_thread_.get_id()!=std::this_thread::get_id()) {
        listen_thread_.join();
    }
}
void TwitchChat::disconnect_socket() {
    socket_connected_ = false;
    boost::system::error_code close_error;
    close_error = socket_->lowest_layer().close(close_error);
    if (close_error) {
        std::cerr << "Socket close error: " << close_error.message() << std::endl;
    }
}

bool TwitchChat::is_connected() const {
    return connected_;
}

void TwitchChat::send_raw(const std::string& line) {
    boost::asio::write(*socket_, boost::asio::buffer(line));
}

std::string TwitchChat::read_line() {
    // twitch sends every ping ~5 minutes
    boost::system::error_code read_error;
    boost::asio::steady_timer timer(io_context_);
    timer.expires_after(std::chrono::seconds(60*6));  
    timer.async_wait([this](const boost::system::error_code& error) {
        if (!error) {
            disconnect_socket();
        }
    });

    boost::asio::async_read_until(
        *socket_, read_buffer_, "\r\n",
        [&](const boost::system::error_code& error, std::size_t) {
            read_error = error; timer.cancel();
        }
    );

    io_context_.restart();
    io_context_.run();
    if (read_error) {
        throw boost::system::system_error(read_error);
    }

    std::istream stream(&read_buffer_);
    std::string line;
    std::getline(stream, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

void TwitchChat::on_message(MessageCallback on_message) {
    callbacks_.push_back(std::move(on_message));
}


void TwitchChat::read_loop() {
    while (connected_) {
        try {
            std::string line = read_line();
            if (line.rfind("PING", 0) == 0) {
                send_raw("PONG :tmi.twitch.tv\r\n");
                continue;
            }
            if (line.find(" RECONNECT") != std::string::npos) {
                disconnect_socket();
                connected_ = false;
                std::cout << "Twitch chat recconect required" << std::endl;
            }
            if ((line.find("Login authentication failed") != std::string::npos) ||
                (line.find("Login unsuccessful") != std::string::npos)) {
                oauth_.refresh(client_id_, client_secret_);
                disconnect_socket();
                connected_ = false;
            }
            if (!is_privmsg(line)) {
                continue;
            }

            TwitchMessage message = parse_message(line);
            for (const auto& callback : callbacks_) {
                callback(message);
            }
        } catch (const boost::system::system_error& error) {
            if (connected_) {
                std::cerr << "chat read error: " << error.what() << std::endl;
                if (!socket_connected_) {
                    // Probably timeout from twitch
                    // tried to recconect here but its too much of a mess
                    connected_ = false;
                } else {
                    // better to also recconect in case its broken.
                    connected_ = false;
                    disconnect_socket();
                }
            }
        }
    }
}

TwitchMessage TwitchChat::parse_message(const std::string& line) const
{
    TwitchMessage message;
    const std::string marker = " PRIVMSG ";
    std::size_t pos = line.find(marker);

    std::string tmpcut = line.substr(0, pos);
    std::size_t tags_pos = tmpcut.find("@");
    const std::size_t author_data_pos = tmpcut.find(" :");
    std::string msg_tags;
    if (tags_pos != std::string::npos) {
        msg_tags = tmpcut.substr(tags_pos+1, author_data_pos-tags_pos-1);
    }
    std::string author_data = tmpcut.substr(author_data_pos+2);
    std::string msg_data = line.substr(pos + marker.size());
    std::string msg_parse_tag;
    bool parse_tags = true;
    while (parse_tags && msg_tags.length()!=0) {
        const std::size_t tag_end = msg_tags.find(";");
        if (tag_end == std::string::npos) {
            msg_parse_tag = msg_tags;
            parse_tags = false;
        } else {
            msg_parse_tag = msg_tags.substr(0, tag_end);
            msg_tags = msg_tags.substr(tag_end+1);
        }
        const std::size_t equal_pos = msg_parse_tag.find("=");
        const std::string tag_name = msg_parse_tag.substr(0, equal_pos);
        std::string tag_value = msg_parse_tag.substr(equal_pos+1);
        if (tag_name=="badge-info") {
            bool parse_tag = true;
            std::string parse_value;
            while (parse_tag && tag_value.length()!=0) {
                const std::size_t split_pos = tag_value.find(",");
                if (split_pos == std::string::npos) {
                    parse_value = tag_value;
                    parse_tag = false;
                } else {
                    parse_value = tag_value.substr(0, split_pos);
                    tag_value = tag_value.substr(split_pos+1);
                }
                const std::size_t badgedata_pos = parse_value.find("/");
                const std::string badge_name = parse_value.substr(0, badgedata_pos);
                const std::string badge_info = parse_value.substr(badgedata_pos+1);
                const auto badge_it = message.author.badges.find(badge_name);
                if (badge_it != message.author.badges.end()) {
                    badge_it->second.info = badge_info;
                } else {
                    TwitchBadgeInfo badge;
                    badge.info = badge_info;
                    message.author.badges[badge_name] = badge;
                }
            }
        } else if (tag_name=="badges") {
            bool parse_tag = true;
            std::string parse_value;
            while (parse_tag && tag_value.length()!=0) {
                std::size_t split_pos = tag_value.find(",");
                if (split_pos == std::string::npos) {
                    parse_value = tag_value;
                    parse_tag = false;
                } else {
                    parse_value = tag_value.substr(0, split_pos);
                    tag_value = tag_value.substr(split_pos+1);
                }
                const std::size_t badgedata_pos = parse_value.find("/");
                const std::string badge_name = parse_value.substr(0, badgedata_pos);
                const std::string badge_version = parse_value.substr(badgedata_pos+1);
                const auto badge_it = message.author.badges.find(badge_name);
                if (badge_it != message.author.badges.end()) {
                    badge_it->second.version = badge_version;
                } else {
                    TwitchBadgeInfo badge;
                    badge.version = badge_version;
                    message.author.badges[badge_name] = badge;
                }
            }
        } else if (tag_name=="client-nonce") {
            // not needed here.
        } else if (tag_name=="color") {
            message.author.color = tag_value;
        } else if (tag_name=="display-name") {
            parse_raw_msg(tag_value, message.author.display_name);
        } else if (tag_name=="first-msg") {
            message.first_message = tag_value=="1";
        } else if (tag_name=="id") {
            message.id = tag_value;
        } else if (tag_name=="mod") {
            message.author.mod = tag_value=="1";
        } else if (tag_name=="returning-chatter") {
            message.author.returning_chatter = tag_value=="1";
        } else if (tag_name=="room-id") { 
            message.room_id = tag_value;
        } else if (tag_name=="subscriber") {
            message.author.sub = tag_value=="1";
        } else if (tag_name=="tmi-sent-ts") {
            message.timestamp = std::stoll(tag_value);
        } else if (tag_name=="turbo") {
            message.author.turbo = tag_value=="1";
        } else if (tag_name=="user-id") {
            message.author.user_id = tag_value;
        } else if (tag_name=="user-type") {
            // old version, too lazy to parse it
        } else if (tag_name=="flags") {
            // not very needed plus urgh parsing
        } else if (tag_name=="emotes") {
            // TODO: hell to implement
        } else if (tag_name=="reply-parent-display-name") {
            parse_raw_msg(tag_value, message.reply_msg.parent_display_name);
        } else if (tag_name=="reply-parent-msg-body") {
            parse_raw_msg(tag_value, message.reply_msg.parent_message);
        } else if (tag_name=="reply-parent-msg-id") {
            message.reply_msg.parent_message_id = tag_value;
        } else if (tag_name=="reply-parent-user-id") {
            message.reply_msg.parent_user_id = tag_value;
        } else if (tag_name=="reply-parent-user-login") {
            message.reply_msg.parent_user_login = tag_value;
        } else if (tag_name=="reply-thread-parent-display-name") {
            parse_raw_msg(tag_value, message.reply_msg.thread_parent_display_name);
        } else if (tag_name=="reply-thread-parent-msg-id") {
            message.reply_msg.thread_parent_message_id = tag_value;
        } else if (tag_name=="reply-thread-parent-user-id") {
            message.reply_msg.thread_parent_user_id = tag_value;
        } else if (tag_name=="reply-thread-parent-user-login") {
            message.reply_msg.thread_parent_user_login = tag_value;
        } else {
            std::cout << "debug unknown tagparse; " << tag_name << " = " << tag_value << std::endl;
        }
    }
    message.responding = !message.reply_msg.parent_message_id.empty();
    
    const std::size_t channel_data_start = msg_data.find("#");
    const std::string msg_data_tmp_cut = msg_data.substr(channel_data_start+1);
    const std::size_t channel_data_end = msg_data_tmp_cut.find(" :");
    message.room_name = msg_data_tmp_cut.substr(0, channel_data_end);
    message.message = msg_data_tmp_cut.substr(channel_data_end+2);
    const std::size_t author_data_split = author_data.find("!");
    message.author.login = author_data.substr(0, author_data_split);
    return message;
}

void TwitchChat::parse_raw_msg(const std::string& raw_msg, std::string& result) const {
    result.clear();
    for (std::size_t i = 0; i < raw_msg.size(); ++i) {
        if (raw_msg[i] != '\\') {
            result += raw_msg[i];
            continue;
        }
        const char next_chat = raw_msg[++i];
        if (next_chat=='s') {
            result += ' ';
        } else if (next_chat=='\\') {
            result += '\\';
        } else if (next_chat==':') {
            result += ';';
        } else if (next_chat=='r') {
            result += '\r';
        } else if (next_chat=='n') {
            result += '\n';
        }
    }   
}

bool TwitchChat::is_privmsg(const std::string& line) const {
    return line.find(" PRIVMSG ") != std::string::npos;
}
