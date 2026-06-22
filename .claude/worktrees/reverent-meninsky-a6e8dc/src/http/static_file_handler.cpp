#include "http/static_file_handler.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace muduo_http {

StaticFileHandler::StaticFileHandler(const std::string& document_root)
    : document_root_(document_root),
      mime_types_(DefaultMimeTypes()) {}

bool StaticFileHandler::Serve(const std::string& url_path, HttpResponse& response) const {
    std::string safe_path = SanitizePath(url_path);

    if (safe_path.empty() || safe_path.back() == '/') {
        if (directory_index_) {
            safe_path += "index.html";
        } else {
            return false;
        }
    }

    std::string full_path = document_root_ + "/" + safe_path;
    if (!IsSafePath(full_path)) {
        return false;
    }

    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    auto dot_pos = url_path.rfind('.');
    std::string extension;
    if (dot_pos != std::string::npos) {
        extension = url_path.substr(dot_pos);
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    }
    std::string mime = GetMimeType(extension);

    response.SetStatusCode(200);
    response.SetStatusMessage("OK");
    response.SetHeader("Content-Type", mime);
    response.SetBody(content);
    return true;
}

void StaticFileHandler::AddMimeType(const std::string& extension, const std::string& mime_type) {
    mime_types_[extension] = mime_type;
}

std::string StaticFileHandler::SanitizePath(const std::string& url_path) const {
    std::string path = url_path;
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path = path.substr(0, qpos);
    auto fpos = path.find('#');
    if (fpos != std::string::npos) path = path.substr(0, fpos);

    std::string cleaned;
    bool last_was_slash = false;
    for (char c : path) {
        if (c == '/') {
            if (!last_was_slash) cleaned += c;
            last_was_slash = true;
        } else {
            cleaned += c;
            last_was_slash = false;
        }
    }

    if (!cleaned.empty() && cleaned[0] == '/') cleaned = cleaned.substr(1);

    std::string final;
    std::istringstream stream(cleaned);
    std::string segment;
    while (std::getline(stream, segment, '/')) {
        if (segment == ".." || segment == ".") continue;
        if (!final.empty()) final += "/";
        final += segment;
    }
    return final;
}

std::string StaticFileHandler::GetMimeType(const std::string& extension) const {
    auto it = mime_types_.find(extension);
    if (it != mime_types_.end()) return it->second;
    return "application/octet-stream";
}

bool StaticFileHandler::IsSafePath(const std::string& absolute_path) const {
    if (absolute_path.find("..") != std::string::npos) return false;
    if (absolute_path.substr(0, document_root_.size()) != document_root_) return false;
    return true;
}

std::unordered_map<std::string, std::string> StaticFileHandler::DefaultMimeTypes() {
    return {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".webp", "image/webp"},
        {".txt", "text/plain; charset=utf-8"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".map", "application/json"},
    };
}

} // namespace muduo_http
