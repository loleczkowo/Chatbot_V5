#include "twitch_auth.hpp"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <json/json.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <iomanip>
#include <chrono>
#include <cerrno>
#include <fcntl.h>

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* output = static_cast<std::string*>(userp);
    output->append(static_cast<char*>(contents), total_size);
    return total_size;
}
std::string json_escape(const std::string& unescaped) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : unescaped) {
        switch (c) {
        case '"':
            out << "\\\""; break;
        case '\\':
            out << "\\\\"; break;
        case '\b':
            out << "\\b"; break;
        case '\f':
            out << "\\f"; break;
        case '\n':
            out << "\\n"; break;
        case '\r':
            out << "\\r"; break;
        case '\t':
            out << "\\t"; break;
        default:
            if (c <= 0x1f) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            } else {
                out << c;
            }
            break;
        }
    }
    out << '"'; return out.str();
}
static Json::Value parse_json(const std::string& text) {
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream(text);
    Json::parseFromStream(builder, stream, &root, &errors);
    return root;
}
std::string url_encode(const std::string& value) {
    CURL* curl = curl_easy_init();
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    std::string result = escaped;
    curl_free(escaped);
    curl_easy_cleanup(curl);
    return result;
}
static std::string get_query_value(const std::string& request, const std::string& key) {
    std::string marker = key + "=";
    std::size_t start = request.find(marker);
    if (start == std::string::npos) {
        return "";
    }
    start += marker.size();
    std::size_t end = request.find_first_of("& \r\n", start);
    if (end == std::string::npos) {return request.substr(start);}
    return request.substr(start, end-start);
}
std::string get_path(const std::string& request) {
    std::size_t start = request.find(' ');
    if (start == std::string::npos) {return "";}
    ++start;

    std::size_t end = request.find(' ', start);
    if (end == std::string::npos) {return "";}
    std::string target = request.substr(start, end - start);
    std::size_t query = target.find('?');

    if (query != std::string::npos) {target.resize(query);}
    return target;
}
std::string post_form(const std::string& url, const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}


TwitchOAuth::TwitchOAuth(
    std::string user_id,
    std::string token,
    std::string refresh_token,
    int64_t expires_at,
    std::unordered_set<std::string> scopes
)
    : user_id_(std::move(user_id)),
      token_(std::move(token)),
      refresh_token_(std::move(refresh_token)),
      expires_at_(expires_at),
      scopes_(std::move(scopes))
{
}
TwitchOAuth::TwitchOAuth(
    const Json::Value& json
)
{
    load_json(json);
}
bool TwitchOAuth::validate() {
    if (std::time(nullptr) > expires_at_-30*60) {return false;}
    CURL* curl = curl_easy_init();
    std::string response;
    long status = 0;
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token_;
    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://id.twitch.tv/oauth2/validate"
    );
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(result));
    }
    if (status==200) {
        const Json::Value json_response = parse_json(response) ;
        if (user_id_.empty()) {
            user_id_ = json_response["user_id"].asString(); 
        } else if (json_response["user_id"].asString() != user_id_) {
            return false;
        }
        return true;
    }
    return false;
}

TwitchOAuth::TwitchOAuthError TwitchOAuth::refresh(const std::string& client_id, const std::string& client_secret) {
    std::string body =
        "grant_type=refresh_token"
        "&refresh_token=" + url_encode(refresh_token_) +
        "&client_id=" + url_encode(client_id) +
        "&client_secret=" + url_encode(client_secret);
    const Json::Value json = parse_json(post_form("https://id.twitch.tv/oauth2/token", body));
    if (!json.isMember("access_token")) {
        const int status = json.get("status", 0).asInt();
        const std::string message = json.get("message", "").asString();
        if (status == 400 && message == "Invalid refresh token") {
            return TwitchOAuthError::ReauthRequired;
        }
        std::cerr << "Twitch OAuth refresh failed: " + json.toStyledString();
        return TwitchOAuthError::Unknown;
    }
    token_ = json["access_token"].asString();
    refresh_token_ = json["refresh_token"].asString();
    expires_at_ = std::time(nullptr) + json["expires_in"].asInt();
    return TwitchOAuthError::Ok;
}

void TwitchOAuth::load_json(const Json::Value& json) {
    token_ = json["access_token"].asString();
    refresh_token_ = json["refresh_token"].asString();
    if (json.isMember("expires_at")) {
        expires_at_ = json["expires_at"].asInt(); 
    } else { 
        expires_at_ = std::time(nullptr) + json["expires_in"].asInt();
    }
    const Json::Value& scope_json = json.isMember("scope") ? json["scope"]:json["scopes"];
    scopes_.clear();
    for (const Json::Value& scope : scope_json) {
        scopes_.insert(scope.asString());
    }
    if (json.isMember("user_id")) {
        user_id_ = json["user_id"].asString();
    }
}

std::string TwitchOAuth::save_json() const {
    std::string out_scopes = "[";
    for (std::unordered_set<std::string>::const_iterator it = scopes_.begin(); it != scopes_.end(); ++it) {
        out_scopes += json_escape(*it);
        if (std::next(it) != scopes_.end()) {out_scopes+=",";}
    }
    out_scopes += "]";
    return
    "{"
        "\"access_token\":"+json_escape(token_)+","
        "\"refresh_token\":"+json_escape(refresh_token_)+","
        "\"expires_at\":"+std::to_string(expires_at_)+","
        "\"user_id\":"+json_escape(user_id_)+","
        "\"scope\":"+out_scopes+
    "}";
}

bool TwitchOAuth::check_scopes(const std::unordered_set<std::string>& scopes) const {
    for (const std::string& scope : scopes) {
        std::unordered_set<std::string>::const_iterator found = scopes_.find(scope);
        if (found == scopes.end()) {return false;}
    }
    return true;
}
std::string TwitchOAuth::user_id() const {return user_id_;}
std::string TwitchOAuth::get_token() const {return token_;}

TwitchAuth::TwitchAuth(
    std::string client_id,
    std::string client_secret,
    int port,
    std::string token_file
)
    : client_id_(std::move(client_id)),
      client_secret_(std::move(client_secret)),
      port_(port),
      token_file_(std::move(token_file))
{
}


std::string TwitchAuth::get_token() {
    if (token_.empty()) {
        token_.clear();
        std::cout << "Re-Fetching AppToken" << std::endl;
        if (!refresh_token()) {std::cerr << "Cannot re-get AppToken" << std::endl;}
    } else if (!validate_token()) {
        std::cout << "Fetching AppToken" << std::endl;
        if (!refresh_token()) {std::cerr << "Cannot get AppToken" << std::endl;}
    }
    return token_;
}

TwitchOAuth* TwitchAuth::get_oauth(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(OAuths_mutex_);
    auto oauth_it = OAuths_.find(user_id);
    if (oauth_it == OAuths_.end()) {
        return nullptr;
    }
    return &oauth_it->second;
}

bool TwitchAuth::refresh_token() {
    const std::string body =
        "grant_type=client_credentials"
        "&client_id=" + url_encode(client_id_) +
        "&client_secret=" + url_encode(client_secret_);
    const std::string response_raw = post_form("https://id.twitch.tv/oauth2/token", body);
    const Json::Value response = parse_json(response_raw);
    if (response.isMember("status")) {
        long status = response["status"].asInt();
        if (status!=200) {
            std::cerr << "Error while getting token; " << response_raw << std::endl;
            return false;
        }
    }
    token_ = response["access_token"].asString();
    token_expires_at_ = std::time(nullptr) + response["expires_in"].asInt();
    return true;
}


bool TwitchAuth::validate_token() {
    if (std::time(nullptr) > token_expires_at_-30*60) {return false;}
    CURL* curl = curl_easy_init();
    std::string response;
    long status = 0;
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token_;
    headers = curl_slist_append(headers, auth_header.c_str());
    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://id.twitch.tv/oauth2/validate"
    );
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(result));
    }
    if (status==200) {
        if (parse_json(response)["client_id"].asString() != client_id_) {
            return false;
        }
        return true;
    }
    return false; 
}


const std::string& TwitchAuth::client_id() const
{
    return client_id_;
}

void TwitchAuth::oauth_check() {
    bool updated = false;
    {

        std::lock_guard<std::mutex> lock(OAuths_mutex_);
        for (auto i_oauth = OAuths_.begin(); i_oauth != OAuths_.end();) {
            TwitchOAuth& oauth = i_oauth->second;
            if (oauth.validate()) {
                ++i_oauth;
                continue;
            }
            std::cout << "Refreshing OAuth for " << oauth.user_id() << std::endl;
            TwitchOAuth::TwitchOAuthError refresh_err = oauth.refresh(client_id_, client_secret_);
            switch (refresh_err) {
            case TwitchOAuth::TwitchOAuthError::Ok:
                updated = true;
                ++i_oauth;
                break;
            case TwitchOAuth::TwitchOAuthError::ReauthRequired:
                ++i_oauth;
                break;
            case TwitchOAuth::TwitchOAuthError::Unknown:
                i_oauth = OAuths_.erase(i_oauth);
                updated = true;
                std::cout << "Refresh failed, removing oauth" << std::endl;
                break;
            }
        }
    }

    if (updated) {save_oauth();}
}

void TwitchAuth::load_oauth() {
    std::ifstream file(token_file_);
    if (!file.is_open()) {return;}
    Json::Value token_data;
    file >> token_data;
    std::lock_guard<std::mutex> lock(OAuths_mutex_);
    for (const Json::Value& oauth_json : token_data) {
        TwitchOAuth oauth = TwitchOAuth(oauth_json);
        OAuths_.insert_or_assign(oauth.user_id(), oauth);
    }
}

void TwitchAuth::save_oauth() const {
    std::ofstream file(token_file_);
    std::lock_guard<std::mutex> lock(OAuths_mutex_);
    file << "[\n";

    for (auto it = OAuths_.begin(); it != OAuths_.end();) {
        file << it->second.save_json();
        if (++it != OAuths_.end()) {file << ",\n";}
    }

    file << "\n]";   
}

std::string TwitchAuth::oauth2_botauth() const {
    return "https://id.twitch.tv/oauth2/authorize"
           "?client_id=" + url_encode(client_id_) +
           "&redirect_uri=" + url_encode(redirect_uri()) +
           "&response_type=code&scope=" + url_encode(
        "chat:read chat:edit user:read:chat user:write:chat user:bot user:read:moderated_channels user:read:emotes "
        "user:read:whispers user:manage:whispers moderation:read moderator:manage:announcements "
        "moderator:manage:automod moderator:read:automod_settings moderator:manage:automod_settings "
        "moderator:read:banned_users moderator:manage:banned_users moderator:read:blocked_terms "
        "moderator:manage:blocked_terms moderator:read:chat_messages moderator:manage:chat_messages "
        "moderator:read:chat_settings moderator:manage:chat_settings moderator:read:chatters "
        "moderator:read:followers moderator:read:moderators moderator:read:shield_mode "
        "moderator:manage:shield_mode moderator:read:shoutouts moderator:manage:shoutouts "
        "moderator:read:suspicious_users moderator:manage:suspicious_users moderator:read:unban_requests "
        "moderator:manage:unban_requests moderator:read:vips moderator:read:warnings moderator:manage:warnings");
}
std::string TwitchAuth::oauth2_broadcasterauth() const {
    return "https://id.twitch.tv/oauth2/authorize"
           "?client_id=" + url_encode(client_id_) +
           "&redirect_uri=" + url_encode(redirect_uri()) +
           "&response_type=code&scope=" + url_encode("channel:bot");
}
std::string TwitchAuth::redirect_uri() const {return "http://localhost:"+std::to_string(port_)+"/callback";}


void TwitchAuth::stop_oauth2_server() {
    oauth_server_connected_.store(false);
    if (oauth_server_thread_.joinable() && oauth_server_thread_.get_id() != std::this_thread::get_id()
    ) {
        oauth_server_thread_.join();
    }
}
void TwitchAuth::start_oauth2_server() {
    if (oauth_server_connected_.exchange(true)) {
        return;
    }
    if (oauth_server_thread_.joinable()) {
        oauth_server_thread_.join();
    }
    oauth_server_thread_ = std::thread(&TwitchAuth::oauth_server, this);
}
bool TwitchAuth::is_oauth2_server_alive() {
    return oauth_server_connected_.load();
}

void TwitchAuth::oauth_server()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        oauth_server_connected_.store(false);
        throw std::runtime_error("Could not create OAuth server socket");
    }
    const std::string body = "Twitch OAuth complete. You can close this tab.";
    const std::string body_404 = "404! What are you doing here!?";
    const std::string response_redirect =
        "HTTP/1.1 303 See Other\r\n"
        "Location: /done\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    const std::string response_404 = 
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body_404.size()) + "\r\n"
        "\r\n" +
        body_404;
    const std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" +
        body;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port_);

    if (bind(
        server_fd,
        reinterpret_cast<sockaddr*>(&address),
    sizeof(address)) < 0) {
        close(server_fd);
        oauth_server_connected_.store(false);
        throw std::runtime_error("Could not bind localhost port");
    }
    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        oauth_server_connected_.store(false);
        throw std::runtime_error("Could not listen on localhost port");
    }
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    while (oauth_server_connected_.load()){
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            std::cerr << "oauth2-serv accept error; " << std::strerror(errno) << std::endl;
            break;
        }

        char buffer[4096];
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        std::string request;
        if (bytes_read > 0) {
            request.assign(buffer, bytes_read);
        }
        const std::string error_query = get_query_value(request, "error");
        const std::string code = get_query_value(request, "code");
        const std::string path = get_path(request);
        if (path == "/done") {
            send(client_fd, response.c_str(), response.size(), 0);
            close(client_fd);
            continue;
        }
        if (path != "/callback") {
            send(client_fd, response_404.c_str(), response_404.size(), 0);
            close(client_fd);
            continue;
        }
        
        send(client_fd, response_redirect.c_str(), response_redirect.size(), 0);
        close(client_fd);
        if (!error_query.empty()) {std::cerr<<"oauth2-serv-err; "<<error_query<<std::endl; continue;}
        if (code.empty()) {std::cerr << "oauth2-serv-err; missing code" << std::endl; continue;}
        std::string body_tokenfetch =
            "client_id=" + url_encode(client_id_) +
            "&client_secret=" + url_encode(client_secret_) +
            "&code=" + url_encode(code) +
            "&grant_type=authorization_code"
            "&redirect_uri=" + url_encode(redirect_uri());
        Json::Value response_tokenfetch = parse_json(post_form(
            "https://id.twitch.tv/oauth2/token",
            body_tokenfetch
        ));

        if (!response_tokenfetch.isMember("access_token")) {
            std::cerr << "oauth2-serv-err (nacct); " << response_tokenfetch << std::endl;
            continue;
        }
        TwitchOAuth new_oauth(response_tokenfetch);
        if (new_oauth.validate()) {
            {
                std::lock_guard<std::mutex> lock(OAuths_mutex_);
                std::cout << "oauth2-serv new oauth for; " << new_oauth.user_id() << std::endl;
                OAuths_.insert_or_assign(new_oauth.user_id(), new_oauth);
            }
            save_oauth();
        }        
    }
    close(server_fd);
    oauth_server_connected_.store(false);
}


std::string TwitchAuth::join_scopes(const std::vector<std::string>& scopes, const std::string& separator) {
    std::string result;
    for (std::size_t i = 0; i < scopes.size(); ++i) {
        if (i != 0) {
            result += separator;
        }
        result += scopes[i];
    }
    return result;
}
