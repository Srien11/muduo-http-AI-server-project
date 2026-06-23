#include "http/user_manager.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#include "http/mcp/json.hpp"

namespace muduo_http {

UserManager::UserManager(const std::string& data_dir)
    : data_dir_(data_dir) {
    mkdir(data_dir_.c_str(), 0755);
}

// ---------- SMS / Phone Auth ----------

bool UserManager::SendSmsCode(const std::string& phone, std::string& err_msg) {
    // Generate 6-digit code
    std::string code;
    srand(static_cast<unsigned>(time(nullptr)));
    for (int i = 0; i < 6; i++) {
        code += '0' + (rand() % 10);
    }

    // Try Alibaba Cloud SMS
    if (SmsConfigured()) {
        std::string sms_err;
        if (!SendAliyunSms(phone, code, sms_err)) {
            std::cerr << "[sms] API error: " << sms_err << "\n";
            std::cout << "\n========================================\n";
            std::cout << "[验证码] " << phone << " → " << code << "\n";
            std::cout << "========================================\n\n";
        } else {
            std::cout << "[sms] sent code to " << phone << "\n";
        }
    } else {
        // Dev mode: print code to server log
        std::cout << "\n========================================\n";
        std::cout << "[验证码] " << phone << " → " << code << "\n";
        std::cout << "========================================\n\n";
    }

    // Store code with 5-minute expiry
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sms_codes_[phone] = code;
        sms_expiry_[phone] = time(nullptr) + 300;  // 5 minutes
    }

    std::cout << "[sms] sent code " << code << " to " << phone << "\n";
    return true;
}

// ---------- Register ----------

bool UserManager::Register(const std::string& phone, const std::string& code,
                           const std::string& password, std::string& err_msg) {
    // Verify code
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sms_codes_.find(phone);
        if (it == sms_codes_.end()) {
            err_msg = "请先获取验证码";
            return false;
        }
        auto expiry_it = sms_expiry_.find(phone);
        if (expiry_it != sms_expiry_.end() && time(nullptr) > expiry_it->second) {
            sms_codes_.erase(it);
            sms_expiry_.erase(expiry_it);
            err_msg = "验证码已过期";
            return false;
        }
        if (it->second != code) {
            err_msg = "验证码错误";
            return false;
        }
        // Code correct, remove it
        sms_codes_.erase(it);
        sms_expiry_.erase(expiry_it);
    }

    // Check if already registered
    {
        UserInfo existing;
        if (LoadUser(phone, existing)) {
            // User exists — this is a password reset scenario, skip
        }
    }

    // Create user
    UserInfo user;
    user.phone = phone;
    user.password_hash = HashPassword(password);
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    user.created_at = std::ctime(&now_t);
    if (!user.created_at.empty() && user.created_at.back() == '\n')
        user.created_at.pop_back();

    if (!SaveUser(user)) {
        err_msg = "保存用户信息失败";
        return false;
    }

    std::cout << "[user] registered: " << phone << "\n";
    return true;
}

// ---------- Login ----------

bool UserManager::Login(const std::string& phone, const std::string& password,
                        std::string& token, std::string& err_msg) {
    UserInfo user;
    if (!LoadUser(phone, user)) {
        err_msg = "用户不存在";
        return false;
    }

    if (user.password_hash != HashPassword(password)) {
        err_msg = "密码错误";
        return false;
    }

    token = GenerateToken();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tokens_[token] = phone;
    }

    return true;
}

// ---------- Token verification ----------

bool UserManager::VerifyToken(const std::string& token, std::string& phone) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tokens_.find(token);
    if (it == tokens_.end()) return false;
    phone = it->second;
    return true;
}

// ---------- Get/Update user ----------

bool UserManager::GetUser(const std::string& phone, UserInfo& user) {
    return LoadUser(phone, user);
}

bool UserManager::UpdateUser(const std::string& phone, const std::string& api_key,
                             const std::string& model, const std::string& api_base) {
    UserInfo user;
    if (!LoadUser(phone, user)) return false;
    if (!api_key.empty()) user.api_key = api_key;
    if (!model.empty()) user.model = model;
    if (!api_base.empty()) user.api_base = api_base;
    return SaveUser(user);
}

// ---------- File I/O ----------

std::string UserManager::UserPath(const std::string& phone) const {
    // Sanitize phone for filename (keep only digits)
    std::string safe;
    for (char c : phone) {
        if (c >= '0' && c <= '9') safe += c;
    }
    return data_dir_ + "/user_" + safe + ".json";
}

bool UserManager::SaveUser(const UserInfo& user) {
    try {
        nlohmann::json j = {
            {"phone", user.phone},
            {"password_hash", user.password_hash},
            {"api_key", user.api_key},
            {"api_base", user.api_base},
            {"model", user.model},
            {"created_at", user.created_at}
        };
        std::ofstream file(UserPath(user.phone));
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (...) { return false; }
}

bool UserManager::LoadUser(const std::string& phone, UserInfo& user) {
    try {
        std::ifstream file(UserPath(phone));
        if (!file.is_open()) return false;
        nlohmann::json j;
        file >> j;
        user.phone = j.value("phone", "");
        user.password_hash = j.value("password_hash", "");
        user.api_key = j.value("api_key", "");
        user.api_base = j.value("api_base", "https://api.deepseek.com/v1");
        user.model = j.value("model", "deepseek-v4-flash");
        user.created_at = j.value("created_at", "");
        return true;
    } catch (...) { return false; }
}

// ---------- Crypto helpers ----------

std::string UserManager::HashPassword(const std::string& password) const {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(password.data()),
           password.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string UserManager::GenerateToken() const {
    // Generate a random token: timestamp + random hex
    std::string data = std::to_string(time(nullptr)) + "-" + std::to_string(rand());
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()),
           data.size(), hash);
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// ---------- Alibaba Cloud SMS API ----------

std::string UserManager::PercentEncode(const std::string& s) const {
    std::ostringstream oss;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::hex << std::uppercase << std::setw(2)
                << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string UserManager::HmacSha1(const std::string& data,
                                   const std::string& key) const {
    unsigned char result[SHA_DIGEST_LENGTH];
    HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(data.data()),
         data.size(), result, nullptr);
    // Base64 encode
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i += 3) {
        unsigned long v = static_cast<unsigned long>(result[i]) << 16;
        if (i + 1 < SHA_DIGEST_LENGTH)
            v |= static_cast<unsigned long>(result[i + 1]) << 8;
        if (i + 2 < SHA_DIGEST_LENGTH)
            v |= static_cast<unsigned long>(result[i + 2]);
        out += b64[(v >> 18) & 0x3F];
        out += b64[(v >> 12) & 0x3F];
        out += b64[(v >> 6) & 0x3F];
        out += b64[v & 0x3F];
    }
    // Pad
    size_t pad = SHA_DIGEST_LENGTH % 3;
    if (pad == 1) { out[out.size() - 1] = '='; out[out.size() - 2] = '='; }
    else if (pad == 2) { out[out.size() - 1] = '='; }
    return out;
}

bool UserManager::SendAliyunSms(const std::string& phone,
                                        const std::string& code,
                                        std::string& err_msg) {
    // Use Alibaba Cloud Number Auth (dypnsapi) SendSmsVerifyCode API
    // No enterprise qualification needed — uses platform-preset signatures
    // Reference: https://help.aliyun.com/document_detail/441392.html

    time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);

    std::string nonce = std::to_string(now) + "-" + std::to_string(rand());

    // Template params: code and expiry in minutes
    std::string template_param = "{\"code\":\"" + code + "\",\"min\":\"5\"}";

    // Build query parameters (sorted alphabetically by key)
    std::map<std::string, std::string> params = {
        {"AccessKeyId", sms_access_key_id_},
        {"Action", "SendSmsVerifyCode"},
        {"Code", code},
        {"ExpireTime", "300"},
        {"Format", "JSON"},
        {"PhoneNumber", phone},
        {"SignName", sms_sign_name_},
        {"SignatureMethod", "HMAC-SHA1"},
        {"SignatureNonce", nonce},
        {"SignatureVersion", "1.0"},
        {"TemplateCode", sms_template_code_},
        {"TemplateParam", template_param},
        {"Timestamp", timestamp},
        {"Version", "2017-05-25"},
    };

    // Build canonicalized query string
    std::string canonicalized;
    for (const auto& [k, v] : params) {
        if (!canonicalized.empty()) canonicalized += "&";
        canonicalized += PercentEncode(k) + "=" + PercentEncode(v);
    }

    // StringToSign = HTTPMethod + "&" + PercentEncode("/") + "&" + PercentEncode(canonicalized)
    std::string string_to_sign = "GET&" + PercentEncode("/") + "&" + PercentEncode(canonicalized);

    // Sign with HMAC-SHA1 using AccessKeySecret + "&"
    std::string signature = HmacSha1(string_to_sign, sms_access_key_secret_ + "&");

    // Final URL — dypnsapi (number auth service, no enterprise qualification needed)
    std::string url = "https://dypnsapi.aliyuncs.com/?" + canonicalized +
                      "&Signature=" + PercentEncode(signature);

    // Use curl to send request
    std::string cmd = "curl -s --max-time 10 --noproxy '*' '" + url + "' 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        err_msg = "网络请求失败";
        return false;
    }

    std::string response;
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) response += buf;
    (void)pclose(pipe);

    // Parse response
    try {
        auto json = nlohmann::json::parse(response);
        std::string biz_id = json.value("BizId", "");
        std::string msg = json.value("Message", "");
        std::string request_id = json.value("RequestId", "");
        std::string code_resp = json.value("Code", "");

        if (code_resp == "OK") {
            return true;
        } else {
            err_msg = "短信发送失败: " + msg + " (" + code_resp + ")";
            return false;
        }
    } catch (...) {
        err_msg = "短信API响应解析失败: " + response.substr(0, 200);
        return false;
    }
}

} // namespace muduo_http
