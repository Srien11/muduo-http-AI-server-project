#include "http/stream_writer.h"

#include <sstream>

namespace muduo_http {

StreamWriter::StreamWriter(const muduo::net::TcpConnectionPtr& conn,
                           int status_code,
                           const std::string& status_message,
                           const std::string& content_type)
    : conn_(conn),
      status_code_(status_code),
      status_message_(status_message),
      content_type_(content_type) {}

void StreamWriter::WriteChunk(const std::string& data) {
    if (!conn_ || !conn_->connected()) return;

    if (!headers_sent_) {
        SendHeaders();
    }

    // Chunked format: <hex-size>\r\n<data>\r\n
    std::ostringstream chunk;
    chunk << std::hex << data.size() << "\r\n" << data << "\r\n";
    conn_->send(chunk.str());
}

void StreamWriter::WriteSSE(const std::string& data) {
    WriteSSE("", data, "");
}

void StreamWriter::WriteSSE(const std::string& event, const std::string& data) {
    WriteSSE(event, data, "");
}

void StreamWriter::WriteSSE(const std::string& event, const std::string& data, const std::string& id) {
    if (!conn_ || !conn_->connected()) return;

    if (!headers_sent_) {
        SendHeaders();
    }

    std::string frame;

    if (!event.empty()) {
        frame += "event: " + event + "\n";
    }
    if (!id.empty()) {
        frame += "id: " + id + "\n";
    }

    // Split data by newlines, prefix each with "data: "
    size_t pos = 0;
    while (pos < data.size()) {
        size_t next = data.find('\n', pos);
        if (next == std::string::npos) {
            frame += "data: " + data.substr(pos) + "\n";
            break;
        }
        frame += "data: " + data.substr(pos, next - pos) + "\n";
        pos = next + 1;
    }

    frame += "\n";

    // Wrap in chunked encoding
    std::ostringstream chunk;
    chunk << std::hex << frame.size() << "\r\n" << frame << "\r\n";
    conn_->send(chunk.str());
}

void StreamWriter::WriteRaw(const std::string& data) {
    if (!conn_ || !conn_->connected()) return;

    if (!headers_sent_) {
        SendHeaders();
    }

    conn_->send(data);
}

void StreamWriter::End() {
    if (!conn_ || !conn_->connected()) return;

    if (!headers_sent_) {
        // No data was sent; send minimal headers + empty chunk
        SendHeaders();
    }

    // Final zero-length chunk + trailing CRLF
    conn_->send("0\r\n\r\n");

    // Close the connection after stream ends
    conn_->shutdown();
}

void StreamWriter::SendHeaders() {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";
    headers << "Content-Type: " << content_type_ << "\r\n";
    headers << "Transfer-Encoding: chunked\r\n";
    headers << "Connection: close\r\n";
    headers << "Cache-Control: no-cache\r\n";
    headers << "\r\n";

    conn_->send(headers.str());
    headers_sent_ = true;
}

} // namespace muduo_http
