#pragma once
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <stdexcept>
#include <string>

using Clock=std::chrono::steady_clock;
struct Timeout: std::runtime_error{
    using std::runtime_error::runtime_error;
};

class WebSocket{
public:
    WebSocket(int port, const std::string& path){
        fd_=socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family=AF_INET;
        addr.sin_port=htons(port);
        addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(connect(fd_, (sockaddr*)&addr, sizeof(addr))<0) throw std::runtime_error("cannot connect to Chrome");

        sendAll("GET " + path + " HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n");
        auto deadline=Clock::now()+std::chrono::seconds(5);
        std::string reply;
        while(reply.find("\r\n\r\n")==std::string::npos) reply+=readExactly(1, deadline);
        if(reply.find(" 101 ")==std::string::npos) throw std::runtime_error("handshake failed:"+reply);
    }

    ~WebSocket(){
        close(fd_);
    }
    WebSocket(const WebSocket&)=delete;
    WebSocket& operator=(const WebSocket&)=delete;

     void send(const std::string& msg) {
        const unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
        std::string frame(1, char(0x81));
        size_t n = msg.size();
        if (n < 126) frame += char(0x80 | n);
        else if (n < 65536) frame += {char(0x80 | 126), char(n >> 8), char(n)};
        else throw std::runtime_error("message too long");
        frame.append((const char*)mask, 4);
        for (size_t i = 0; i < n; i++) frame += char(msg[i] ^ mask[i % 4]);
        sendAll(frame);
    }

    std::string receive(Clock::time_point deadline) {
        std::string head = readExactly(2, deadline);
        int opcode = head[0] & 0x0F;
        size_t n = head[1] & 0x7F;
        if (n >= 126) {
            std::string ext = readExactly(n == 126 ? 2 : 8, deadline);
            n = 0;
            for (unsigned char c : ext) n = (n << 8) | c;
        }
        std::string payload = readExactly(n, deadline);
        if (opcode == 0x8) throw std::runtime_error("Chrome closed the connection");
        return payload;
    }

private:
    int fd_;

    void sendAll(const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            ssize_t k = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (k <= 0) throw std::runtime_error("send failed");
            sent += k;
        }
    }

    std::string readExactly(size_t n, Clock::time_point deadline) {
        std::string out(n, '\0');
        size_t got = 0;
        while (got < n) {
            int ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            pollfd p{fd_, POLLIN, 0};
            if (ms <= 0 || poll(&p, 1, ms) <= 0) throw Timeout("timed out waiting for Chrome");
            ssize_t k = recv(fd_, &out[got], n - got, 0);
            if (k <= 0) throw std::runtime_error("connection closed");
            got += k;
        }
        return out;
    }
};