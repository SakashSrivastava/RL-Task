#include <iostream>
#include "websocket.hpp"

int main(int argc, char** argv) {
    WebSocket ws(9222, argv[1]);
    ws.send(R"({"id": 1, "method": "Browser.getVersion"})");
    std::cout << ws.receive(Clock::now() + std::chrono::seconds(2)) << "\n";
}