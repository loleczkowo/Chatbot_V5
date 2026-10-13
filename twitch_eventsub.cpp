#include "twitch_eventsub.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/system/error_code.hpp>

#include <openssl/ssl.h>
#include <json/json.h>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

//using ErrorCode = boost::system::error_code;

TwitchEventSub::TwitchEventSub()
    : ssl_context_(boost::asio::ssl::context::tls_client)
{
    ssl_context_.set_default_verify_paths();
    ssl_context_.set_verify_mode(boost::asio::ssl::verify_peer);
}
TwitchEventSub::TwitchEventSub(
    const std::vector<MessageCallback>& callbacks, std::string host
)
    : callbacks_(callbacks),
      host_(std::move(host)),
      ssl_context_(boost::asio::ssl::context::tls_client)
{
    ssl_context_.set_default_verify_paths();
    ssl_context_.set_verify_mode(boost::asio::ssl::verify_peer);
}
TwitchEventSub::~TwitchEventSub() {
    disconnect();
}
void TwitchEventSub::connect() {
    if (connected_) return;
    if (listen_thread_.joinable()) disconnect();
    io_context_.restart();
    try {
        boost::asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, "443");

        ws_ = std::make_unique<WebSocket>(io_context_, ssl_context_);
        boost::beast::websocket::stream_base::timeout options {
            std::chrono::seconds(30), std::chrono::seconds(60), false
        };
        ws_->set_option(options);
        boost::asio::connect(ws_->next_layer().next_layer(), endpoints);

        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            throw boost::system::system_error(
                static_cast<int>(ERR_get_error()),
                boost::asio::error::get_ssl_category()
            );
        }

        ws_->next_layer().set_verify_callback(boost::asio::ssl::host_name_verification(host_));
        ws_->next_layer().handshake(boost::asio::ssl::stream_base::client);

        ws_->handshake(host_, "/ws");
        connected_ = true;
        read_loop();
        listen_thread_ = std::thread([this]() {
            io_context_.run();
        });
    }
    catch (const std::exception& e) {
        std::cerr << "EventSub connection failed: " << e.what() << '\n';
        connected_ = false;
    }
}

void TwitchEventSub::read_loop() {
    ws_->async_read(read_buffer_, [this](boost::system::error_code const& ec, std::size_t){
        if (ec) {
            if (ec == boost::beast::error::timeout) {
                std::cerr << "Eventsub read_loop timeout\n";
            } else if (
                ec != boost::beast::websocket::error::closed &&
                ec != boost::asio::error::operation_aborted
            ) {
                std::cerr << "EventSub read_loop failed: " << ec.what() << '\n';
            }
            connected_ = false; welcomed_ = false; return;
        }
        std::string raw = boost::beast::buffers_to_string(read_buffer_.data());
        read_buffer_.consume(read_buffer_.size());

        Json::Value message;
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream stream(raw);
        if (!Json::parseFromStream(builder, stream, &message, &errors)) {
            std::cerr << "EventSub JSON error: " << errors << '\n';
        } else {
            std::string type = message["metadata"]["message_type"].asString();
            if (type == "session_welcome") {
                connection_id_ = message["payload"]["session"]["id"].asString();
                std::cout << "EventSub welcomed!" << std::endl; welcomed_ = true;
            } else if (type == "session_keepalive") {
            } else if (type == "notification") {
                const Json::Value& payload = message["payload"];
                for (const MessageCallback& callback : callbacks_) {callback(payload);}
            } else if (type == "session_reconnect") {
                swap = message["payload"]["session"]["reconnect_url"].asString();
            } else {
                std::cout << "EventSub unknown type (" << type << "):\n    " << raw << std::endl;
            }
        }
        if (connected_) read_loop();
    });
}
void TwitchEventSub::disconnect() {
    if (listen_thread_.joinable()) {
        if (!io_context_.stopped()) {
            boost::asio::post(io_context_, [this]() {
                ws_->async_close(boost::beast::websocket::close_code::normal, [this](boost::system::error_code const& ec){
                    if (ec && ec != boost::asio::ssl::error::stream_truncated)
                        std::cerr << "EventSub closing error: "<< ec.what() << '\n';
                });
            });
        }
        listen_thread_.join();
    }
    connected_ = false; welcomed_ = false;
}

void TwitchEventSub::on_message(MessageCallback on_message) {
    callbacks_.push_back(std::move(on_message));
}
