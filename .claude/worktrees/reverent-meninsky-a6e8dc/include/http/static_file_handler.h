#pragma once

#include <string>
#include <unordered_map>

#include "http/http_request.h"
#include "http/http_response.h"

namespace muduo_http {

class StaticFileHandler {
public:
    explicit StaticFileHandler(const std::string& document_root);

    // Serve a file. Returns true if file was found and served.
    bool Serve(const std::string& url_path, HttpResponse& response) const;

    // Register a custom MIME type
    void AddMimeType(const std::string& extension, const std::string& mime_type);

    // Enable/disable directory index (index.html)
    void set_directory_index(bool enabled) { directory_index_ = enabled; }

private:
    std::string SanitizePath(const std::string& url_path) const;
    std::string GetMimeType(const std::string& extension) const;
    bool IsSafePath(const std::string& absolute_path) const;
    static std::unordered_map<std::string, std::string> DefaultMimeTypes();

    std::string document_root_;
    std::unordered_map<std::string, std::string> mime_types_;
    bool directory_index_{true};
};

} // namespace muduo_http
