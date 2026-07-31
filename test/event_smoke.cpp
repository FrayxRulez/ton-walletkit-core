// Unsolicited updates: the core pushing rather than answering. Events reach
// twk_receive with request_id = 0, are distinguishable from responses, and
// interleave with them without corrupting correlation. Returns 0 on pass.
#include <cstdio>
#include <string>
#include <vector>

#include "twk/twk.h"
#include "twk/twk_delegates.h"

namespace {

void on_http(void* /*user*/, twk_client* client, twk_token token, const char* /*method*/, const char* /*url*/,
             const char* /*headers*/, const char* /*body*/) {
    // No network needed here; answer everything with an empty object.
    twk_http_respond(client, token, 200, "{\"content-type\":\"application/json\"}", "{}");
}

bool contains(const std::string& s, const char* needle) {
    return s.find(needle) != std::string::npos;
}

struct Message {
    unsigned long long request_id;
    std::string json;
};

Message receive(twk_client* c, double timeout = 30.0) {
    unsigned long long rid = 0;
    const char* out = twk_receive(c, timeout, &rid);
    return {rid, out ? std::string(out) : std::string()};
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    twk_delegates delegates{};
    delegates.http_request = on_http;
    twk_client* c = twk_client_create(&delegates, nullptr);

    bool ok = true;

    twk_send(c, 1, "initWalletKit", "[{\"networks\":[{\"chainId\":\"-3\"}]}]");
    Message m = receive(c);
    bool got = m.request_id == 1 && contains(m.json, "\"result\"");
    printf("%s: initWalletKit\n", got ? "ok" : "FAIL");
    ok = ok && got;

    // A pushed event must arrive as an update: request_id 0, {"event":…} envelope.
    twk_send(c, 2, "emitTestEvent", "[\"connectRequest\",{\"id\":\"abc\",\"from\":\"dapp\"}]");

    // Two messages arrive: the event (id 0) and the call's own response (id 2).
    // Order is not guaranteed, so classify rather than assume.
    Message first = receive(c);
    Message second = receive(c);

    const Message& event = first.request_id == 0 ? first : second;
    const Message& response = first.request_id == 0 ? second : first;

    got = event.request_id == 0 && contains(event.json, "\"event\"") &&
          contains(event.json, "\"type\":\"connectRequest\"") && contains(event.json, "\"id\":\"abc\"");
    printf("%s: event delivered with request_id=0 -> %.110s\n", got ? "ok" : "FAIL", event.json.c_str());
    ok = ok && got;

    got = response.request_id == 2 && contains(response.json, "\"result\"");
    printf("%s: the request's own response still correlates (id=%llu)\n", got ? "ok" : "FAIL",
           response.request_id);
    ok = ok && got;

    // Interleaving: several events plus several requests, all accounted for and
    // correctly attributed.
    const int kRounds = 5;
    for (int i = 0; i < kRounds; ++i) {
        twk_send(c, static_cast<unsigned long long>(100 + i), "emitTestEvent", "[\"tick\",{\"n\":1}]");
        twk_send(c, static_cast<unsigned long long>(200 + i), "echo", "[\"x\"]");
    }

    int events = 0, responses = 0;
    std::vector<unsigned long long> ids;
    for (int i = 0; i < kRounds * 3; ++i) { // 5 events + 5 emit responses + 5 echo responses
        Message msg = receive(c, 10.0);
        if (msg.json.empty()) {
            break;
        }
        if (msg.request_id == 0) {
            ++events;
        } else {
            ++responses;
            ids.push_back(msg.request_id);
        }
    }

    got = events == kRounds && responses == kRounds * 2;
    printf("%s: interleaved (%d events, %d responses; expected %d/%d)\n", got ? "ok" : "FAIL", events,
           responses, kRounds, kRounds * 2);
    ok = ok && got;

    // Every response id must be one we sent — no cross-talk with events.
    bool ids_ok = true;
    for (unsigned long long id : ids) {
        ids_ok = ids_ok && ((id >= 100 && id < 105) || (id >= 200 && id < 205));
    }
    printf("%s: response ids intact\n", ids_ok ? "ok" : "FAIL");
    ok = ok && ids_ok;

    twk_client_destroy(c);
    printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
