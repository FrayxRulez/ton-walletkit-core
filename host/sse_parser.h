//
// Incremental text/event-stream parser (header-only so it can be unit-tested).
//
// Framing per the SSE spec: "field: value" lines; `data` accumulates across
// lines joined by \n; a blank line dispatches the accumulated event; lines
// starting with ':' are comments (relays send them as keep-alives); and `id`
// persists as the stream's last-event-id until the server changes it — TON
// Connect resumes from it after a reconnect.
//
#pragma once

#include <functional>
#include <string>

namespace twk {
namespace refhost {

class SseParser {
public:
    // Called once per dispatched event with (data, event, id).
    using Dispatch = std::function<void(const std::string& data, const std::string& event, const std::string& id)>;

    explicit SseParser(Dispatch dispatch) : dispatch_(std::move(dispatch)) {}

    // Feed arbitrary chunks; events may span chunk boundaries.
    void feed(const char* bytes, size_t len) {
        buffer_.append(bytes, len);
        size_t start = 0;
        for (;;) {
            size_t end = buffer_.find('\n', start);
            if (end == std::string::npos) {
                break;
            }
            std::string line = buffer_.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back(); // tolerate CRLF
            }
            start = end + 1;
            handleLine(line);
        }
        buffer_.erase(0, start);
    }

    void feed(const std::string& chunk) { feed(chunk.data(), chunk.size()); }

private:
    void handleLine(const std::string& line) {
        if (line.empty()) {
            flush();
            return;
        }
        if (line[0] == ':') {
            return; // keep-alive comment
        }

        size_t colon = line.find(':');
        std::string field = colon == std::string::npos ? line : line.substr(0, colon);
        std::string value = colon == std::string::npos ? std::string() : line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(value.begin()); // a single leading space is part of the framing
        }

        if (field == "data") {
            if (!data_.empty()) {
                data_ += '\n';
            }
            data_ += value;
        } else if (field == "event") {
            event_ = value;
        } else if (field == "id") {
            id_ = value;
        }
        // "retry" is ignored: reconnection is walletkit's concern, not the host's.
    }

    void flush() {
        if (data_.empty() && event_.empty()) {
            return; // a blank line with nothing accumulated dispatches nothing
        }
        dispatch_(data_, event_.empty() ? "message" : event_, id_);
        data_.clear();
        event_.clear();
        // id_ deliberately persists (spec: last-event-id).
    }

    Dispatch dispatch_;
    std::string buffer_, data_, event_, id_;
};

} // namespace refhost
} // namespace twk
