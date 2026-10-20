#include "twitch_api.hpp"

#include <curl/curl.h>
#include <json/json.h>

#include <iostream>
#include <string>
#include <iomanip>


//Json::Value TwitchApi::test() {
//    const std::string broadcaster_id = get_id("loleczkowo");
//    const std::string sender_id = get_id(bot_nickname_, true);
//    const std::string body =
//        "{"
//            "\"broadcaster_id\":\"" + broadcaster_id + "\","
//            "\"sender_id\":\"" + sender_id + "\","
//            "\"message\":\"test123123\""
//        "}";
//    return post("https://api.twitch.tv/helix/chat/messages", body);
//}
std::string TwitchApi::test() {
    set_stream(get_id("loleczkowo"), get_category_id("Forts"), "test");
    return "";
}


TwitchApi::TwitchApi(
    TwitchAuth& auth,
    std::string bot_nickname
)
    : auth_(auth),
    curl_(curl_easy_init())
{
    bot_id_ = get_id(bot_nickname);
}

TwitchApi::~TwitchApi() {
    if (curl_ != nullptr) {curl_easy_cleanup(curl_);}
}


bool channel_token(std::string& token, TwitchAuth& auth_, const std::unordered_set<std::string>& scopes, const std::string& id) {
    const TwitchLockedOAuth oauth = auth_.get_oauth(id);
    if (oauth == nullptr) return false;
    if (!oauth->check_scopes(scopes)) return false;
    token = oauth->get_token(); return true;
}
enum TokenSource {
    NO_TOKEN, BOT_TOKEN, CHANNEL_TOKEN, APP_TOKEN
};


Json::Value TwitchApi::get_streams(const std::vector<std::string>& user_logins) {
    Json::Value streams;
    for (size_t i = 0; i < user_logins.size(); i += 100) {
        std::string request_params = "first=100";  // twitch limit for 2026
        for (size_t j = i; j < std::min(i+100, user_logins.size()); j++) {
            request_params += "&user_login="+user_logins[j];
        }
        Json::Value result = get("https://api.twitch.tv/helix/streams?"+request_params);
        for (auto stream_data : result["data"]) {
            streams.append(stream_data);
        }
    }
    return streams;
}
std::string TwitchApi::get_category_id(const std::string& category) {
    Json::Value result = get("https://api.twitch.tv/helix/games?name="+url_encode(category));
    if (!result.isMember("data")) return "";
    if (result["data"].empty()) return "";
    if (!result["data"][0].isMember("id")) return "";
    return result["data"][0]["id"].asString();
}
Json::Value TwitchApi::send_message(const std::string& room_id, const std::string& message) {
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + room_id + "\","
            "\"sender_id\":\"" + bot_id_ + "\","
            "\"message\":" + json_escape(message) +
        "}";
    return post("https://api.twitch.tv/helix/chat/messages", body, room_id, {"channel:bot"}, {"user:write:chat"});
}
Json::Value TwitchApi::send_message(const std::string& room_id, const std::string& message, const std::string& reply_id) {
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + room_id + "\","
            "\"sender_id\":\"" + bot_id_ + "\","
            "\"reply_parent_message_id\":\"" + reply_id + "\"," +
            "\"message\":" + json_escape(message) +
        "}";
    return post("https://api.twitch.tv/helix/chat/messages", body, room_id, {"channel:bot"}, {"user:write:chat"});
}
void TwitchApi::set_stream(const std::string& room_id, const std::string& category_id, const std::string& title) {
    if (category_id.empty() && title.empty()) {return;}
    std::string body = "{";
    if (!category_id.empty()) {
        body += "\"game_id\":\""+category_id+'"';
        if (!title.empty()) body += ',';
    }
    if (!title.empty()) body += "\"title\":"+json_escape(title);
    body += '}';
    for (int i=0;i<2;i++) {
        long status_code = 0;
        std::string token;
        if (!channel_token(token, auth_, {"channel:manage:broadcast"}, room_id)) {
            std::cerr << "TwitchApi; no matching token for channel:manage:broadcast (category) for " << room_id << '\n'; return;
        }       
        raw_post("https://api.twitch.tv/helix/channels?broadcaster_id="+room_id, token, body, status_code, "PATCH");
        if (status_code == 401) {auth_.oauth_check(room_id);} else {break;}
    }
}
bool TwitchApi::run_ads(const std::string& room_id, unsigned int length, ChannelContext& channel_context) {
    const time_t now = std::time(nullptr);
    if (now < channel_context.next_ad) return false;
    const std::string body =
        "{"
            "\"broadcaster_id\":\"" + room_id + "\","
            "\"length\":" + std::to_string(length) +
        "}";
    Json::Value root;
    long status_code = 0;
    for (int i=0;i<2;i++) {
        std::string token;
        if (!channel_token(token, auth_, {"channel:edit:commercial"}, room_id)) {
            std::cerr << "TwitchApi; no matching token for channel:edit:commercial (run ads) for " << room_id << '\n'; return false;
        }
        root = raw_post("https://api.twitch.tv/helix/channels/commercial", token, body, status_code);
        if (status_code == 401) {auth_.oauth_check(room_id);} else {break;}
    }
    Json::Value data;
    if (root.isMember("data") && !root["data"].empty()) data = root["data"][0];
    if (data.isMember("retry_after")) channel_context.next_ad = data["retry_after"].asInt64()+now;
    if (status_code == 400) {
        std::cout << "TwitchApi-run_ads; Warning 400; ";
        if (data.isMember("message")) {std::cout << data["message"].asString() << std::endl;}
        else {std::cout << root << std::endl;}
    }
    if (status_code != 200) return false;
    return true;
}
void TwitchApi::eventsub_subscribe(
    const std::string& type, const std::string& version, const std::string& condition,
    const std::string& session_id, const std::string& room_id,
    const std::unordered_set<std::string>& scopes, const std::unordered_set<std::string>& bot_scopes
) {
    const std::string body = 
        "{"
            "\"type\":\"" + type + "\","
            "\"version\":\"" + version + "\","
            "\"condition\":" + condition + ","
            "\"transport\":{"
                "\"method\":\"websocket\","
                "\"session_id\":\"" + session_id + "\""
            "}"
        "}";
    Json::Value root;
    for (int i=0;i<2;i++) {
        long status_code = 0;
        std::string token;
        TokenSource source = NO_TOKEN;
        if      (channel_token(token, auth_, scopes, room_id)) source = CHANNEL_TOKEN;
        else if (channel_token(token, auth_, bot_scopes, bot_id_)) source = BOT_TOKEN;
        else {std::cerr << "TwitchApi-Eventsub; no matching token for " << type << "\n"; return;}
        root = raw_post("https://api.twitch.tv/helix/eventsub/subscriptions", token, body, status_code);
        if (status_code == 401) {
            if (source == CHANNEL_TOKEN) auth_.oauth_check(room_id);
            if (source == BOT_TOKEN)     auth_.oauth_check(bot_id_);
        } else {break;}
    }
    if (!root.isMember("status")) return;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = ""; // If you want whitespace-less output
    std::cout << Json::writeString(builder, root) << std::endl;
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


Json::Value TwitchApi::get(const std::string& link, const std::string& working_channel_id, const std::unordered_set<std::string>& scopes, const std::unordered_set<std::string>& bot_scopes) {
    // for chat specific requests.
    Json::Value root;
    std::string use_token;
    bool using_bot_oauth = false;
    bool channel_has_scopes = false;
    if (!working_channel_id.empty()) {
        const TwitchLockedOAuth working_channel_oauth = auth_.get_oauth(working_channel_id);
        if (working_channel_oauth!=nullptr) {channel_has_scopes = working_channel_oauth->check_scopes(scopes);}
    }
    if (!scopes.empty() && !channel_has_scopes) {
        // We do not have the needed channel oauth so we fallback to bot's user token (If we have one)
        const TwitchLockedOAuth bot_oauth = auth_.get_oauth(bot_id_);
        if (bot_oauth == nullptr || !bot_oauth->check_scopes(bot_scopes)) {
            std::cerr << "Missing needed OAuth2 tokens for request; " << link << std::endl << std::endl;
            return root;
        }
        use_token = bot_oauth->get_token();
        using_bot_oauth = true;
    } else {
        use_token = auth_.get_token();
    }
    long status_code = 0;
    root = raw_get(link, use_token, status_code);
    if (status_code == 401) {
        std::cout << "Twitch api fail by wrong token, retrying" << std::endl;
        if (using_bot_oauth) {
            auth_.oauth_check(bot_id_);
            const TwitchLockedOAuth bot_oauth = auth_.get_oauth(bot_id_);
            if (bot_oauth==nullptr) {return root;}
            use_token = bot_oauth->get_token();
        } else {
            auth_.refresh_token();
            use_token = auth_.get_token();
        }
        root = raw_get(link, use_token, status_code);
        if (status_code == 401) {std::cerr << "Still Twitch api fail" << std::endl;}
    }
    return root;
}

Json::Value TwitchApi::raw_get(const std::string& link, const std::string& token, long& status_code) {
    Json::Value root;
    if (curl_ == nullptr) {return root;}
    std::lock_guard<std::mutex> lock(curl_mutex_);
    curl_easy_reset(curl_);

    CURLcode res;
    std::string readBuffer;    
    struct curl_slist* headers = nullptr;
    const std::string clientid = "Client-ID: "+auth_.client_id();

    const std::string bearer = "Authorization: Bearer "+token;
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


Json::Value TwitchApi::post(const std::string& link, const std::string& body, const std::string& working_channel_id, const std::unordered_set<std::string>& scopes, const std::unordered_set<std::string>& bot_scopes) {
    Json::Value root;
    std::string use_token;
    bool using_bot_oauth = false;
    bool channel_has_scopes = false;
    if (!working_channel_id.empty()) {
        const TwitchLockedOAuth working_channel_oauth = auth_.get_oauth(working_channel_id);
        if (working_channel_oauth!=nullptr) {channel_has_scopes = working_channel_oauth->check_scopes(scopes);}
    }
    if (!scopes.empty() && !channel_has_scopes) {
        // We do not have the needed channel oauth so we fallback to bot's user token (If we have one)
        const TwitchLockedOAuth bot_oauth = auth_.get_oauth(bot_id_);
        if (bot_oauth == nullptr || !bot_oauth->check_scopes(bot_scopes)) {
            std::cerr << "Missing needed OAuth2 tokens for request; " << link << std::endl << std::endl;
            return root;
        }
        use_token = bot_oauth->get_token();
        using_bot_oauth = true;
    } else {
        use_token = auth_.get_token();
    }
    long status_code = 0;
    root = raw_post(link, use_token, body, status_code);
    if (status_code == 401) {
        std::cerr << "Twitch api fail by wrong token, retrying" << std::endl;
        if (using_bot_oauth) {
            auth_.oauth_check(bot_id_);
            const TwitchLockedOAuth bot_oauth = auth_.get_oauth(bot_id_);
            if (bot_oauth==nullptr) {return root;}
            use_token = bot_oauth->get_token();
        } else {
            auth_.refresh_token();
            use_token = auth_.get_token();
        }
        root = raw_post(link, use_token, body, status_code);
        if (status_code == 401) {std::cerr << "Still Twitch api fail" << std::endl;}
    }
    return root;
}


Json::Value TwitchApi::raw_post(const std::string& link, const std::string& token, const std::string& body, long& status_code, const std::string& method) {
    Json::Value root;
    
    if (curl_ == nullptr) {return root;}
    std::lock_guard<std::mutex> lock(curl_mutex_);
    curl_easy_reset(curl_);

    CURLcode res;
    std::string readBuffer;    
    struct curl_slist* headers = nullptr;
    const std::string content_type = "Content-Type: application/json";
    const std::string clientid = "Client-ID: "+auth_.client_id();
    const std::string bearer = "Authorization: Bearer "+token;
    headers = curl_slist_append(headers, clientid.c_str());
    headers = curl_slist_append(headers, bearer.c_str());
    headers = curl_slist_append(headers, content_type.c_str());

    curl_easy_setopt(curl_, CURLOPT_URL, link.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method.c_str());
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
std::string TwitchApi::url_encode(const std::string& str) {
    const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}
