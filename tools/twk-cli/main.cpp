//
// twk-cli — a tiny desktop driver for ton-walletkit-core (the `tdcli` analog).
//
// Reads request lines from a file (argv[1]) or stdin and sends them; a background
// thread prints every message received back. It is the manual + scripted E2E
// driver used from M0 onward.
//
// Line format:   <request_id> <method> [params_json]
//   - params_json is the rest of the line (JSON object | array | string | number),
//     or empty for none.
//   - blank lines and lines starting with '#' are ignored.
//   - "quit" / "exit" stops reading.
//
// Output:   ">> id=<id> method=<m>"   on send
//           "<< id=<rid> <json>"      on receive
//
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "twk/twk.h"

static void receiveLoop(twk_client* client, std::atomic<bool>* running) {
    while (running->load()) {
        unsigned long long rid = 0;
        const char* out = twk_receive(client, 0.1, &rid);
        if (out) {
            std::printf("<< id=%llu %s\n", rid, out);
            std::fflush(stdout);
        }
    }
}

int main(int argc, char** argv) {
    twk_client* client = twk_client_create(nullptr, nullptr);

    std::atomic<bool> running{true};
    std::thread rx(receiveLoop, client, &running);

    std::ifstream file;
    std::istream* in = &std::cin;
    if (argc > 1) {
        file.open(argv[1]);
        if (!file) {
            std::fprintf(stderr, "twk-cli: cannot open %s\n", argv[1]);
            running.store(false);
            rx.join();
            twk_client_destroy(client);
            return 2;
        }
        in = &file;
    }

    std::string line;
    while (std::getline(*in, line)) {
        // Trim trailing CR (scripts authored on Windows) and surrounding blanks.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line == "quit" || line == "exit") {
            break;
        }

        std::istringstream ss(line);
        unsigned long long id = 0;
        std::string method;
        ss >> id >> method;

        std::string params;
        std::getline(ss, params);
        const size_t start = params.find_first_not_of(" \t");
        params = (start == std::string::npos) ? std::string() : params.substr(start);

        std::printf(">> id=%llu method=%s\n", id, method.c_str());
        std::fflush(stdout);
        twk_send(client, id, method.c_str(), params.empty() ? nullptr : params.c_str());
    }

    // Give in-flight responses a moment to arrive, then tear down cleanly:
    // stop the receive thread and join it BEFORE destroying the client.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    running.store(false);
    rx.join();
    twk_client_destroy(client);
    return 0;
}
