#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace muduo_http {

struct MultipartField {
    std::string name;
    std::string value;          // form field value
    std::string filename;       // empty if not a file
    std::string content_type;   // Content-Type of the part
    std::string data;           // raw data
};

class MultipartParser {
public:
    // Parse multipart/form-data request body
    // boundary is extracted from Content-Type header
    bool Parse(const std::string& body, const std::string& boundary);

    // Access parsed fields
    bool HasField(const std::string& name) const;
    std::string GetFieldValue(const std::string& name) const;

    // Access uploaded files
    bool HasFile(const std::string& name) const;
    const MultipartField* GetFile(const std::string& name) const;

    const std::vector<MultipartField>& fields() const { return fields_; }
    std::vector<MultipartField>&& TakeFields() { return std::move(fields_); }

private:
    bool ParseHeaders(const std::string& header_block, std::unordered_map<std::string, std::string>& headers);
    std::string DecodeUrl(const std::string& encoded);

    std::vector<MultipartField> fields_;
};

// Extract boundary from Content-Type header value
// e.g. "multipart/form-data; boundary=----WebKitFormBoundary"
std::string ExtractBoundary(const std::string& content_type);

} // namespace muduo_http
