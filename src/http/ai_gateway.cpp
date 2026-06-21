#include "http/ai_gateway.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>

namespace muduo_http {

// -------- Callback for libcurl body write --------
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total);
    return total;
}

// -------- AiResponseCache --------

AiResponseCache::AiResponseCache(size_t max_entries, int ttl_seconds)
    : max_entries_(max_entries), ttl_seconds_(ttl_seconds) {}

bool AiResponseCache::Get(const std::string& key, std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(key);
    if (it == cache_.end()) return false;

    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at).count();
    if (age >= ttl_seconds_) {
        cache_.erase(it);
        return false;
    }

    value = it->second.value;
    return true;
}

void AiResponseCache::Set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Evict if full
    while (cache_.size() >= max_entries_) {
        // Find oldest entry
        auto oldest = cache_.begin();
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->second.created_at < oldest->second.created_at) {
                oldest = it;
            }
        }
        cache_.erase(oldest);
    }

    Entry entry;
    entry.value = value;
    entry.created_at = std::chrono::steady_clock::now();
    cache_[key] = entry;
}

void AiResponseCache::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

// -------- AiGateway --------

AiGateway::AiGateway(AiConfig config)
    : config_(std::move(config)),
      rate_limiter_(config_.rate_limit_rps, config_.rate_limit_burst) {
    curl_global_init(CURL_GLOBAL_ALL);
    if (config_.cache_enabled) {
        cache_ = std::make_unique<AiResponseCache>(config_.cache_max_entries, config_.cache_ttl_seconds);
    }
}

AiChatResponse AiGateway::Chat(const AiChatRequest& request) {
    AiChatResponse response;

    // Rate limit check
    if (!rate_limiter_.Allow()) {
        response.error_message = "rate limit exceeded";
        return response;
    }

    // Build cache key
    std::string cache_key;
    if (cache_) {
        cache_key = BuildCacheKey(request);
        if (cache_->Get(cache_key, response.content)) {
            response.success = true;
            return response;
        }
    }

    // Build request
    std::string request_body = BuildRequestBody(request);

    // libcurl request
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error_message = "failed to initialize curl";
        return response;
    }

    std::string response_body;
    std::string url = config_.api_base + "/chat/completions";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "muduo_http_ai/0.1");

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        response.error_message = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return response;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code != 200) {
        response.error_message = "API returned HTTP " + std::to_string(http_code) + ": " + response_body;
        return response;
    }

    response = ParseResponse(response_body);
    if (response.success && cache_ && !cache_key.empty()) {
        cache_->Set(cache_key, response.content);
    }

    return response;
}

void AiGateway::ChatStream(const AiChatRequest& request, StreamWriter& writer) {
    // Rate limit check
    if (!rate_limiter_.Allow()) {
        writer.WriteSSE("error", "rate limit exceeded");
        writer.End();
        return;
    }

    std::string request_body = BuildRequestBody(request);

    CURL* curl = curl_easy_init();
    if (!curl) {
        writer.WriteSSE("error", "failed to init curl");
        writer.End();
        return;
    }

    std::string url = config_.api_base + "/chat/completions";
    std::string buffer;  // Accumulate partial chunks

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("Authorization: Bearer " + config_.api_key).c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "muduo_http_ai/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        writer.WriteSSE("error", std::string("curl: ") + curl_easy_strerror(res));
        writer.End();
        return;
    }

    // Parse the buffered response and forward tokens via SSE
    // For non-streaming fallback, send whole response at once
    auto parsed = ParseResponse(buffer);
    if (parsed.success) {
        writer.WriteSSE("token", parsed.content);
    } else {
        writer.WriteSSE("error", parsed.error_message);
    }

    writer.WriteSSE("done", "complete");
    writer.End();
}

std::string AiGateway::BuildRequestBody(const AiChatRequest& request) {
    std::ostringstream body;
    body << "{";
    body << "\"model\": \"" << (request.model.empty() ? config_.model : request.model) << "\",";
    body << "\"messages\": [";

    for (size_t i = 0; i < request.messages.size(); ++i) {
        if (i > 0) body << ",";
        body << "{";
        body << "\"role\": \"" << request.messages[i].role << "\",";
        body << "\"content\": \"" << request.messages[i].content << "\"";
        body << "}";
    }

    body << "],";
    body << "\"max_tokens\": " << (request.max_tokens > 0 ? request.max_tokens : config_.max_tokens) << ",";
    body << "\"temperature\": " << request.temperature;
    body << "}";

    return body.str();
}

AiChatResponse AiGateway::ParseResponse(const std::string& body) {
    AiChatResponse response;

    // Simple JSON field extraction (without full JSON parser)
    // Find "content": "..."
    auto content_pos = body.find("\"content\":\"");
    if (content_pos == std::string::npos) {
        content_pos = body.find("\"content\": \"");
    }

    if (content_pos != std::string::npos) {
        content_pos = body.find('"', content_pos + 10);  // Skip past "content": "
        if (content_pos != std::string::npos) {
            auto end_pos = body.find('"', content_pos + 1);
            // Handle escaped quotes
            while (end_pos != std::string::npos && end_pos > 0 && body[end_pos - 1] == '\\') {
                end_pos = body.find('"', end_pos + 1);
            }
            if (end_pos != std::string::npos) {
                response.content = body.substr(content_pos + 1, end_pos - content_pos - 1);
                response.success = true;
            }
        }
    }

    // Extract token usage
    auto prompt_pos = body.find("\"prompt_tokens\":");
    if (prompt_pos != std::string::npos) {
        response.prompt_tokens = std::stoi(body.substr(prompt_pos + 15));
    }
    auto completion_pos = body.find("\"completion_tokens\":");
    if (completion_pos != std::string::npos) {
        response.completion_tokens = std::stoi(body.substr(completion_pos + 18));
    }

    if (!response.success) {
        // Try to extract error message
        auto error_pos = body.find("\"message\":\"");
        if (error_pos == std::string::npos) {
            error_pos = body.find("\"message\": \"");
        }
        if (error_pos != std::string::npos) {
            error_pos = body.find('"', error_pos + 10);
            if (error_pos != std::string::npos) {
                auto end_err = body.find('"', error_pos + 1);
                if (end_err != std::string::npos) {
                    response.error_message = body.substr(error_pos + 1, end_err - error_pos - 1);
                }
            }
        } else {
            response.error_message = "failed to parse response";
        }
    }

    return response;
}

std::string AiGateway::BuildCacheKey(const AiChatRequest& request) {
    std::string key;
    for (const auto& msg : request.messages) {
        key += msg.role + ":" + msg.content + "|";
    }
    key += request.model;
    return key;
}

int AiGateway::rate_limit_remaining() const {
    return 0; // Simplified: real impl would expose token count
}

} // namespace muduo_http
