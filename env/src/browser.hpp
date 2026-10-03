#pragma once
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>
#include "websocket.hpp"

using json = nlohmann::ordered_json;

class Browser {
public:
    explicit Browser(const std::string& chromePath) {
        int pipeFd[2];
        if (pipe(pipeFd) < 0) throw std::runtime_error("pipe failed");
        pid_ = fork();
        if (pid_ == 0) {
            setpgid(0, 0);
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            dup2(pipeFd[1], STDERR_FILENO);
            execlp(chromePath.c_str(), chromePath.c_str(), "--headless", "--remote-debugging-port=0",
                   "--no-first-run", "--no-sandbox", "--disable-gpu", "--disable-background-networking",
                   "--window-size=1280,900", "about:blank", (char*)nullptr);
            _exit(1);
        }
        close(pipeFd[1]);
        try {
            connectToChrome(pipeFd[0]);
        } catch (...) {
            stop();
            throw;
        }
    }

    ~Browser() { stop(); }
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    json call(const std::string& method, const json& params, Clock::time_point deadline) {
        int id = ++lastId_;
        ws_->send(json{{"id", id}, {"method", method}, {"params", params}}.dump());
        while (true) {
            json reply = json::parse(ws_->receive(deadline));
            if (reply.value("id", 0) != id) continue;
            if (reply.contains("error")) throw std::runtime_error(method + " failed: " + reply["error"].dump());
            return reply["result"];
        }
    }

private:
    pid_t pid_;
    int lastId_ = 0;
    std::unique_ptr<WebSocket> ws_;

    void connectToChrome(int stderrFd) {
        FILE* err = fdopen(stderrFd, "r");
        char line[512], path[256];
        int port = 0;
        bool found = false;
        while (!found && fgets(line, sizeof(line), err))
            found = sscanf(line, "DevTools listening on ws://127.0.0.1:%d%255s", &port, path) == 2;
        fclose(err);
        if (!found) throw std::runtime_error("Chrome did not start (check the --chrome path)");

        auto deadline = Clock::now() + std::chrono::seconds(10);
        ws_ = std::make_unique<WebSocket>(port, path);
        std::string target = call("Target.createTarget", {{"url", "about:blank"}}, deadline)["targetId"];
        ws_ = std::make_unique<WebSocket>(port, "/devtools/page/" + target);
    }

    void stop() {
        ws_.reset();
        kill(-pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
    }
};
