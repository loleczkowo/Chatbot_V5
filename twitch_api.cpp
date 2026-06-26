#include "twitch_api.hpp"

#include <curl/curl.h>
#include <json/json.h>

#include <iostream>
#include <string>
#include <iomanip>


Json::Value TwitchApi::test() {
    const std::string broadcaster_id = get_id("feinberg");
    const std::string sender_id = get_id(bot_nickname_, true);
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + broadcaster_id + "\","
            "\"sender_id\":\"" + sender_id + "\","
            "\"message\":\"test123123\""
        "}";
    return post("https://api.twitch.tv/helix/chat/messages", body);
}




TwitchApi::TwitchApi(
    TwitchAuth& auth,
    std::string bot_nickname
)
    : auth_(auth),
    bot_nickname_(bot_nickname),
    curl_(curl_easy_init())
{
}

TwitchApi::~TwitchApi() {
    if (curl_ != nullptr) {curl_easy_cleanup(curl_);}
}


Json::Value TwitchApi::send_message(const std::string& room_id, const std::string& message) {
    const std::string sender_id = get_id(bot_nickname_, true);
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + room_id + "\","
            "\"sender_id\":\"" + sender_id + "\","
            "\"message\":" + json_escape(message) +
        "}";
    return post("https://api.twitch.tv/helix/chat/messages", body);
}
Json::Value TwitchApi::send_message(const std::string& room_id, const std::string& message, const std::string& reply_id) {
    const std::string sender_id = get_id(bot_nickname_, true);
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + room_id + "\","
            "\"sender_id\":\"" + sender_id + "\","
            "\"reply_parent_message_id\":\"" + reply_id + "\"," +
            "\"message\":" + json_escape(message) +
        "}";
    return post("https://api.twitch.tv/helix/chat/messages", body);
}


std::string TwitchApi::get_id(const std::string& nickname, const bool cache) {
    auto id = ids_cache_.find(nickname);
    if (id == ids_cache_.end()) {
        const Json::Value data = get("https://api.twitch.tv/helix/users?login="+nickname)["data"];
        const std::string new_id = data[0]["id"].asString();
        if (cache) {
            ids_cache_[nickname] = new_id;
        }
        return new_id;
    } else {
        return id->second;
    }
}


static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}


Json::Value TwitchApi::get(const std::string& link) {
    long status_code = 0;
    Json::Value root = raw_get(link, status_code);
    if (status_code == 401) {
        std::cout << "Twitch api fail by wrong token, retrying" << std::endl;
        auth_.refresh_token();
        root = raw_get(link, status_code);
    }
    return root;
}

Json::Value TwitchApi::raw_get(const std::string& link, long& status_code) {
    Json::Value root;
    if (curl_ == nullptr) {return root;}
    std::lock_guard<std::mutex> lock(curl_mutex_);
    curl_easy_reset(curl_);

    CURLcode res;
    std::string readBuffer;    
    struct curl_slist* headers = nullptr;
    const std::string clientid = "Client-ID: "+auth_.client_id();
    const std::string bearer = "Authorization: Bearer "+auth_.get_token();
    headers = curl_slist_append(headers, clientid.c_str());
    headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl_, CURLOPT_URL, link.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl_);
    
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    if (res != CURLE_OK) {
        std::cerr << "curl failed: " << curl_easy_strerror(res) << '\n';
        return root;
    }
    Json::Reader reader;
    reader.parse(readBuffer.c_str(), root );
    return root;
}

Json::Value TwitchApi::post(const std::string& link, const std::string& body) {
    long status_code = 0;
    Json::Value root = raw_post(link, body, status_code);
    if (status_code == 401) {
        std::cerr << "Twitch api fail by wrong token, retrying" << std::endl;
        auth_.refresh_token();
        root = raw_post(link, body, status_code);
    }
    return root;
}

Json::Value TwitchApi::raw_post(const std::string& link, const std::string& body, long& status_code) {
    Json::Value root;
    
    if (curl_ == nullptr) {return root;}
    std::lock_guard<std::mutex> lock(curl_mutex_);
    curl_easy_reset(curl_);

    CURLcode res;
    std::string readBuffer;    
    struct curl_slist* headers = nullptr;
    const std::string content_type = "Content-Type: application/json";
    const std::string clientid = "Client-ID: "+auth_.client_id();
    const std::string bearer = "Authorization: Bearer "+auth_.get_token();
    headers = curl_slist_append(headers, clientid.c_str());
    headers = curl_slist_append(headers, bearer.c_str());
    headers = curl_slist_append(headers, content_type.c_str());

    curl_easy_setopt(curl_, CURLOPT_URL, link.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl_);
    
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status_code);
    curl_slist_free_all(headers);
    if (res != CURLE_OK) {
        std::cerr << "curl failed: " << curl_easy_strerror(res) << '\n';
        return root;
    }
    Json::Reader reader;
    reader.parse(readBuffer.c_str(), root );
    return root;
}



std::string TwitchApi::json_escape(const std::string& unescaped)
{
    std::ostringstream out;
    out << '"';

    for (unsigned char c : unescaped) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c <= 0x1f) {
                out << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<int>(c)
                    << std::dec;
            } else {
                out << c;
            }
            break;
        }
    }

    out << '"';
    return out.str();
}
