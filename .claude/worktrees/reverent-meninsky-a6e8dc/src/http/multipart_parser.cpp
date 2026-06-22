#include "http/multipart_parser.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace muduo_http {

bool MultipartParser::Parse(const std::string& body, const std::string& boundary) {
    fields_.clear();

    if (body.empty() || boundary.empty()) return false;

    std::string delimiter = "--" + boundary;
    std::string end_delimiter = "--" + boundary + "--";
    size_t pos = 0;

    while (true) {
        // Find next boundary
        size_t part_start = body.find(delimiter, pos);
        if (part_start == std::string::npos) break;

        // Check if this is the closing boundary
        if (body.substr(part_start, end_delimiter.size()) == end_delimiter) break;

        // Move past the boundary line
        size_t data_start = body.find("\r\n", part_start);
        if (data_start == std::string::npos) break;
        data_start += 2;  // skip \r\n

        // Find the next boundary
        size_t next_boundary = body.find("\r\n--" + boundary, data_start);
        if (next_boundary == std::string::npos) break;

        // Extract the part (headers + body)
        std::string raw_part = body.substr(data_start, next_boundary - data_start);
        if (raw_part.empty()) break;

        // Find the header/body separator (\r\n\r\n)
        size_t header_end = raw_part.find("\r\n\r\n");
        if (header_end == std::string::npos) continue;

        std::string header_block = raw_part.substr(0, header_end);
        std::string part_body = raw_part.substr(header_end + 4);  // skip \r\n\r\n

        // Remove trailing \r\n before the next boundary
        if (part_body.size() >= 2 && part_body.substr(part_body.size() - 2) == "\r\n") {
            part_body.erase(part_body.size() - 2);
        }

        // Parse the Content-Disposition header to get field name & filename
        MultipartField field;
        field.data = part_body;

        std::unordered_map<std::string, std::string> headers;
        ParseHeaders(header_block, headers);

        // Parse Content-Disposition
        std::string disposition = headers["Content-Disposition"];
        if (disposition.empty()) continue;

        // Extract name="..."
        {
            auto name_pos = disposition.find("name=\"");
            if (name_pos != std::string::npos) {
                name_pos += 6;
                auto name_end = disposition.find('"', name_pos);
                if (name_end != std::string::npos) {
                    field.name = disposition.substr(name_pos, name_end - name_pos);
                }
            }
        }

        // Extract filename="..."
        {
            auto file_pos = disposition.find("filename=\"");
            if (file_pos != std::string::npos) {
                file_pos += 10;
                auto file_end = disposition.find('"', file_pos);
                if (file_end != std::string::npos) {
                    field.filename = disposition.substr(file_pos, file_end - file_pos);
                }
            }
        }

        field.content_type = headers["Content-Type"];

        if (!field.filename.empty()) {
            // File upload: keep raw data
            field.value = "(file: " + field.filename + ", " + std::to_string(part_body.size()) + " bytes)";
        } else {
            // Form field: text value
            field.value = part_body;
        }

        fields_.push_back(std::move(field));

        // Move to next part
        pos = next_boundary + 2;  // skip \r\n
    }

    return !fields_.empty();
}

bool MultipartParser::HasField(const std::string& name) const {
    for (const auto& f : fields_) {
        if (f.name == name && f.filename.empty()) return true;
    }
    return false;
}

std::string MultipartParser::GetFieldValue(const std::string& name) const {
    for (const auto& f : fields_) {
        if (f.name == name && f.filename.empty()) return f.value;
    }
    return "";
}

bool MultipartParser::HasFile(const std::string& name) const {
    for (const auto& f : fields_) {
        if (f.name == name && !f.filename.empty()) return true;
    }
    return false;
}

const MultipartField* MultipartParser::GetFile(const std::string& name) const {
    for (const auto& f : fields_) {
        if (f.name == name && !f.filename.empty()) return &f;
    }
    return nullptr;
}

bool MultipartParser::ParseHeaders(const std::string& header_block,
                                    std::unordered_map<std::string, std::string>& headers) {
    std::istringstream stream(header_block);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);

        headers[key] = value;
    }
    return !headers.empty();
}

std::string ExtractBoundary(const std::string& content_type) {
    auto pos = content_type.find("boundary=");
    if (pos == std::string::npos) return "";

    std::string boundary = content_type.substr(pos + 9);

    // Remove optional quotes
    if (!boundary.empty() && boundary.front() == '"') {
        boundary = boundary.substr(1);
    }
    if (!boundary.empty() && boundary.back() == '"') {
        boundary.pop_back();
    }

    // Remove trailing whitespace or semicolon
    while (!boundary.empty() &&
           (boundary.back() == ' ' || boundary.back() == ';' || boundary.back() == '\r')) {
        boundary.pop_back();
    }

    return boundary;
}

} // namespace muduo_http
