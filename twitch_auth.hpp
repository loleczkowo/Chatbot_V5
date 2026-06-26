#pragma once

#include <json/json.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>

class TwitchOAuth {
public:
    enum class TwitchOAuthError {
        Ok,
        ReauthRequired,
        Unknown
    };
    TwitchOAuth(
        std::string user_id,
        std::string token,
        std::string refresh_token,
        int64_t expires_at,
        std::unordered_set<std::string> scopes
    );
    TwitchOAuth(
        const Json::Value& json
    );
    bool validate();
    TwitchOAuthError refresh(const std::string& client_id, const std::string& client_secret);
    std::string save_json() const;
    void load_json(const Json::Value& json);
    
    bool check_scopes(const std::unordered_set<std::string>& scopes) const;
    std::string user_id() const;
    std::string get_token() const;
private:
    std::string user_id_;
    std::string token_;
    std::string refresh_token_;
    int64_t expires_at_;
    std::unordered_set<std::string> scopes_;
};

class TwitchAuth {
public:
    TwitchAuth(
        std::string client_id,
        std::string client_secret,
        int port = 5000,
        std::string token_file = "twitch_oauths.json"
    );

    std::string get_token();
    TwitchOAuth* get_oauth(const std::string& user_id); 
    bool refresh_token();
    bool validate_token();
    void oauth_check();
    void load_oauth();
    void save_oauth() const;

    void start_oauth2_server();
    void stop_oauth2_server();
    bool is_oauth2_server_alive();
    // oauth2 auth links
    std::string oauth2_botauth() const;
    std::string oauth2_broadcasterauth() const;
    std::string redirect_uri() const;

    const std::string& client_id() const;
private:
    std::string client_id_;
    std::string client_secret_;
    int port_;
    std::string token_file_;
    std::string token_;
    int64_t token_expires_at_;
    std::unordered_map<std::string, TwitchOAuth> OAuths_;
    mutable std::mutex OAuths_mutex_;
    
    std::atomic_bool oauth_server_connected_ = false; 
    std::thread oauth_server_thread_;
    void oauth_server();

    static std::string join_scopes(
        const std::vector<std::string>& scopes,
        const std::string& separator
    );
};