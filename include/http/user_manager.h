#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace muduo_http {

struct UserInfo {
    std::string phone;
    std::string password_hash;
    std::string api_key;
    std::string api_base = "https://api.deepseek.com/v1";
    std::string model = "deepseek-v4-flash";
    std::string created_at;
};

class UserManager {
public:
    explicit UserManager(const std::string& data_dir = "users");

    // Send SMS verification code via Alibaba Cloud
    bool SendSmsCode(const std::string& phone, std::string& err_msg);

    // Register: verify code + create account
    bool Register(const std::string& phone, const std::string& code,
                  const std::string& password, std::string& err_msg);

    // Login: verify password, return token
    bool Login(const std::string& phone, const std::string& password,
               std::string& token, std::string& err_msg);

    // Verify token, return phone
    bool VerifyToken(const std::string& token, std::string& phone);

    // Get user info
    bool GetUser(const std::string& phone, UserInfo& user);

    // Update user settings (api_key, model, api_base)
    bool UpdateUser(const std::string& phone, const std::string& api_key,
                    const std::string& model, const std::string& api_base);

    // Configure Alibaba Cloud SMS credentials
    void SetSmsConfig(const std::string& access_key_id,
                      const std::string& access_key_secret,
                      const std::string& sign_name,
                      const std::string& template_code) {
        sms_access_key_id_ = access_key_id;
        sms_access_key_secret_ = access_key_secret;
        sms_sign_name_ = sign_name;
        sms_template_code_ = template_code;
    }

    bool SmsConfigured() const {
        return !sms_access_key_id_.empty() && !sms_access_key_secret_.empty();
    }

private:
    std::string UserPath(const std::string& phone) const;
    std::string HashPassword(const std::string& password) const;
    std::string GenerateToken() const;
    bool SaveUser(const UserInfo& user);
    bool LoadUser(const std::string& phone, UserInfo& user);

    // Alibaba Cloud SMS API helpers
    std::string PercentEncode(const std::string& s) const;
    std::string HmacSha1(const std::string& data, const std::string& key) const;
    bool SendAliyunSms(const std::string& phone, const std::string& code,
                              std::string& err_msg);

    std::string data_dir_;
    std::unordered_map<std::string, std::string> sms_codes_;   // phone -> code
    std::unordered_map<std::string, time_t> sms_expiry_;       // phone -> expiry
    std::unordered_map<std::string, std::string> tokens_;      // token -> phone
    mutable std::mutex mutex_;

    // Alibaba Cloud SMS config
    std::string sms_access_key_id_;
    std::string sms_access_key_secret_;
    std::string sms_sign_name_;
    std::string sms_template_code_;
};

} // namespace muduo_http
