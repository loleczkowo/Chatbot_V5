#pragma once

#include "twitch_auth.hpp"

#include <curl/curl.h>
#include <json/json.h>

#include <mutex>
#include <unordered_map>
#include <string>


class TwitchApi {
public:
    TwitchApi(
        TwitchAuth& auth,
        std::string bot_nickname
    );
    ~TwitchApi();
    std::string test();
    Json::Value get_streams(const std::vector<std::string>& user_logins);
    std::string get_category_id(const std::string& category);
    Json::Value send_message(const std::string& room_id, const std::string& message);
    Json::Value send_message(const std::string& room_id, const std::string& message, const std::string& reply_id);
    void set_stream(const std::string& room_id, const std::string& category_id="", const std::string& title="");
    void eventsub_subscribe(
        const std::string& type, const std::string& version, const std::string& condition,
        const std::string& session_id, const std::string& room_id,
        const std::unordered_set<std::string>& scopes, const std::unordered_set<std::string>& bot_scopes
    );
    std::string get_id(const std::string& nickname, const bool cache=false);

private:
    TwitchAuth& auth_;
    std::string bot_id_;
    CURL* curl_ = nullptr;
    std::mutex curl_mutex_;
    std::unordered_map<std::string, std::string> ids_cache_;
    std::string json_escape(const std::string& unescaped);
    std::string url_encode(const std::string& str);
    Json::Value get(const std::string& link, const std::string& working_channel_id="", const std::unordered_set<std::string>& scopes={}, const std::unordered_set<std::string>& bot_scopes={});
    Json::Value post(const std::string& link, const std::string& body, const std::string& working_channel_id="", const std::unordered_set<std::string>& scopes={}, const std::unordered_set<std::string>& bot_scopes={});
    Json::Value raw_get(const std::string& link, const std::string& token, long& status_code);
    Json::Value raw_post(const std::string& link, const std::string& token, const std::string& body, long& status_code, const std::string& method="POST");
};
