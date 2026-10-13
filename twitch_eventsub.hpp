#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>

#include <json/json.h>

#include <functional>
#include <string>
#include <vector>
#include <thread>

class TwitchEventSub {
public:
    using MessageCallback = std::function<void(const Json::Value&)>;
    TwitchEventSub();
    TwitchEventSub(const std::vector<MessageCallback>& callbacks, std::string host);
    ~TwitchEventSub();
    
    void connect();
    void disconnect();
    bool is_connected() {return connected_;}
    bool welcomed() {return welcomed_;}
    std::string connection_id() {return connection_id_;}

    void on_message(MessageCallback on_message);
    std::vector<MessageCallback> callbacks() {return callbacks_;}
    std::string swap;
private:
    bool connected_ = false;
    bool welcomed_ = false;
    std::string connection_id_;

    std::vector<MessageCallback> callbacks_;

    std::thread listen_thread_;
    void read_loop();

    std::string host_ = "eventsub.wss.twitch.tv";
    boost::asio::io_context io_context_;
    boost::asio::ssl::context ssl_context_;
    using WebSocket = boost::beast::websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
    std::unique_ptr<WebSocket> ws_;

    boost::beast::flat_buffer read_buffer_;
};
