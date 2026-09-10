#include "transport/http_message.hpp"

#include <sstream>

namespace forge::transport {

HttpResponse json_response(int status, std::string status_text, std::string json_body) {
    HttpResponse response;
    response.status = status;
    response.status_text = std::move(status_text);
    response.headers["content-type"] = "application/json";
    response.body = std::move(json_body);
    return response;
}

HttpResponse plain_text_response(int status, std::string status_text, std::string text_body) {
    HttpResponse response;
    response.status = status;
    response.status_text = std::move(status_text);
    response.headers["content-type"] = "text/plain; charset=utf-8";
    response.body = std::move(text_body);
    return response;
}

std::string render_response(const HttpResponse& response) {
    std::ostringstream out;
    out << "HTTP/1.1 " << response.status << ' ' << response.status_text << "\r\n";
    for (const auto& [name, value] : response.headers) {
        if (name == "content-length" || name == "connection") {
            continue; // set below, always, regardless of what the handler put there
        }
        out << name << ": " << value << "\r\n";
    }
    out << "content-length: " << response.body.size() << "\r\n";
    out << "connection: close\r\n";
    out << "\r\n";
    out << response.body;
    return out.str();
}

} // namespace forge::transport
