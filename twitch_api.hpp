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
    Json::Value test();
    Json::Value send_message(const std::string& room_id, const std::string& message);
    Json::Value send_message(const std::string& room_id, const std::string& message, const std::string& reply_id);
    std::string get_id(const std::string& nickname, const bool cache=false);

private:
    TwitchAuth& auth_;
    std::string bot_nickname_;
    CURL* curl_ = nullptr;
    std::mutex curl_mutex_;
    std::unordered_map<std::string, std::string> ids_cache_;
    std::string json_escape(const std::string& unescaped);
    Json::Value get(const std::string& link, const std::string& working_channel_id="", const std::unordered_set<std::string>& scopes={}, const std::unordered_set<std::string>& bot_scopes={});
    Json::Value post(const std::string& link, const std::string& body, const std::string& working_channel_id="", const std::unordered_set<std::string>& scopes={}, const std::unordered_set<std::string>& bot_scopes={});
    Json::Value raw_get(const std::string& link, const std::string& token, long& status_code);
    Json::Value raw_post(const std::string& link, const std::string& token, const std::string& body, long& status_code);
};
