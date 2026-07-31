// Unit tests for the text/event-stream framing parser. This is the fiddly part
// of SSE — multi-line data, comments, CRLF, chunk boundaries, id persistence —
// so it is tested directly rather than only through a live stream. Returns 0.
#include <cstdio>
#include <string>
#include <vector>

#include "sse_parser.h"

using twk::refhost::SseParser;

namespace {

struct Event {
    std::string data, type, id;
};

struct Collector {
    std::vector<Event> events;
    SseParser parser;

    Collector()
        : parser([this](const std::string& data, const std::string& type, const std::string& id) {
              events.push_back({data, type, id});
          }) {}
};

bool expect(const char* name, bool condition, const std::string& detail = {}) {
    printf("%s: %s%s%s\n", condition ? "ok" : "FAIL", name, detail.empty() ? "" : " -> ", detail.c_str());
    return condition;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    bool ok = true;

    {   // Basic event: dispatched only on the blank line, defaults to "message".
        Collector c;
        c.parser.feed("data: hello\n");
        ok &= expect("no dispatch before the blank line", c.events.empty());
        c.parser.feed("\n");
        ok &= expect("basic event", c.events.size() == 1 && c.events[0].data == "hello" &&
                                        c.events[0].type == "message");
    }

    {   // Named type and id.
        Collector c;
        c.parser.feed("event: connect\nid: 42\ndata: payload\n\n");
        ok &= expect("event type + id", c.events.size() == 1 && c.events[0].type == "connect" &&
                                            c.events[0].id == "42" && c.events[0].data == "payload");
    }

    {   // Multi-line data joins with \n.
        Collector c;
        c.parser.feed("data: line1\ndata: line2\n\n");
        ok &= expect("multi-line data", c.events.size() == 1 && c.events[0].data == "line1\nline2",
                     c.events.empty() ? "" : c.events[0].data);
    }

    {   // Comments (relay keep-alives) are ignored and do not dispatch.
        Collector c;
        c.parser.feed(": keep-alive\n\n");
        ok &= expect("comment dispatches nothing", c.events.empty());
        c.parser.feed(": ping\ndata: real\n\n");
        ok &= expect("comment alongside data", c.events.size() == 1 && c.events[0].data == "real");
    }

    {   // CRLF line endings.
        Collector c;
        c.parser.feed("data: crlf\r\n\r\n");
        ok &= expect("CRLF handled", c.events.size() == 1 && c.events[0].data == "crlf",
                     c.events.empty() ? "" : c.events[0].data);
    }

    {   // Events split across arbitrary chunk boundaries (the network case).
        Collector c;
        std::string stream = "data: split-ac";
        c.parser.feed(stream);
        c.parser.feed("ross\nid: 7\n");
        ok &= expect("no premature dispatch mid-chunk", c.events.empty());
        c.parser.feed("\n");
        ok &= expect("event spanning chunks", c.events.size() == 1 && c.events[0].data == "split-across" &&
                                                  c.events[0].id == "7");
    }

    {   // id persists as last-event-id until the server changes it (spec), which
        // is what lets TON Connect resume after a reconnect.
        Collector c;
        c.parser.feed("id: 1\ndata: first\n\ndata: second\n\nid: 9\ndata: third\n\n");
        ok &= expect("id persists across events",
                     c.events.size() == 3 && c.events[0].id == "1" && c.events[1].id == "1" &&
                         c.events[2].id == "9");
    }

    {   // Several events in one chunk.
        Collector c;
        c.parser.feed("data: a\n\ndata: b\n\ndata: c\n\n");
        ok &= expect("batched events", c.events.size() == 3 && c.events[2].data == "c");
    }

    {   // A value with no leading space, and a field with no colon.
        Collector c;
        c.parser.feed("data:tight\n\n");
        ok &= expect("no leading space", c.events.size() == 1 && c.events[0].data == "tight");
    }

    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
