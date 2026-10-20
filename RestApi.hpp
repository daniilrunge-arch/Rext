#pragma once
#include "Config.hpp"
#include "JsonText.hpp"
#include "Session.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <chrono>
#include <iostream>
#include <cstring>
#include <vector>

class RestApi {
public:
    RestApi(Config& cfg,
            std::mutex& mtx,
            std::unordered_map<int, std::shared_ptr<Session>>& sessions)
        : cfg_(cfg), mtx_(mtx), sessions_(sessions) {
        start_ = std::chrono::steady_clock::now();
    }

    void start() {
        running_ = true;
        th_ = std::thread([this] { loop(); });
    }
    void stop() {
        running_ = false;
        if (listen_ != INVALID_SOCK) {
#ifdef _WIN32
            shutdown(listen_, SD_BOTH);
            closesocket(listen_);
#else
            shutdown(listen_, SHUT_RDWR);
            close(listen_);
#endif
            listen_ = INVALID_SOCK;
        }
        if (th_.joinable() && th_.get_id() != std::this_thread::get_id()) th_.join();
        while(workers_.load())std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

private:
    Config& cfg_;
    std::mutex& mtx_;
    std::unordered_map<int, std::shared_ptr<Session>>& sessions_;
    std::thread th_;
    std::atomic<unsigned> workers_{0};
    std::atomic<bool> running_{false};
    socket_t listen_ = INVALID_SOCK;
    std::chrono::steady_clock::time_point start_;

    void loop() {
        listen_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int one = 1;
        setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(uint16_t(cfg_.restPort));
        if (bind(listen_, (sockaddr*)&a, sizeof(a)) < 0 || listen(listen_, 32) < 0) {
            std::cerr << "[REST] bind/listen failed on " << cfg_.restPort << "\n";
            return;
        }
        std::cout << "[REST] http://0.0.0.0:" << cfg_.restPort << "/v2/server/status\n";
        while (running_) {
            socket_t c = accept(listen_, nullptr, nullptr);
            if (c == INVALID_SOCK) continue;
#ifdef _WIN32
            DWORD timeout=1000;setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
#else
            timeval timeout{1,0};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#endif
            ++workers_;
            std::thread([this, c] { handle(c);--workers_; }).detach();
        }
    }

    void handle(socket_t c) {
        char buf[2048]{};
#ifdef _WIN32
        int n = recv(c, buf, sizeof(buf) - 1, 0);
#else
        ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
#endif
        if (n <= 0) {
#ifdef _WIN32
            closesocket(c);
#else
            close(c);
#endif
            return;
        }
        std::string req(buf, size_t(n));
        bool ok = req.find("/v2/server/status") != std::string::npos
               || req.find("/v2/status") != std::string::npos
               || req.find("GET / ") != std::string::npos
               || req.find("GET / HTTP") != std::string::npos;

        std::string body;
        if (ok) {
            std::vector<std::string> players;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                for (auto& kv : sessions_)
                    if (!kv.second->closed && !kv.second->username.empty())
                        players.push_back(kv.second->username);
            }
            auto up = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_).count();
            int days = int(up / 86400), hours = int((up % 86400) / 3600);
            int mins = int((up % 3600) / 60), secs = int(up % 60);
            std::ostringstream ps;
            ps << "[";
            for (size_t i = 0; i < players.size(); ++i) {
                if (i) ps << ",";
                ps << jsonQuote(players[i]);
            }
            ps << "]";
            std::ostringstream js;
            js << "{"
               << "\"status\":\"200\","
               << "\"name\":" << jsonQuote(cfg_.restName) << ","
               << "\"serverversion\":" << jsonQuote(cfg_.restVersion) << ","
               << "\"tshockversion\":\"6.2.1.0\","
               << "\"port\":" << (cfg_.restServerPort?cfg_.restServerPort:cfg_.listenPort) << ","
               << "\"playercount\":" << players.size() << ","
               << "\"maxplayers\":" << cfg_.restMaxPlayers << ","
               << "\"world\":" << jsonQuote(cfg_.restWorld) << ","
               << "\"uptime\":\"" << days << "." << (hours<10?"0":"") << hours
               << ":" << (mins<10?"0":"") << mins << ":" << (secs<10?"0":"") << secs << "\","
               << "\"serverpassword\":" << (cfg_.restHasPassword?"true":"false") << ","
               << "\"players\":" << ps.str()
               << "}";
            body = js.str();
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
#ifdef _WIN32
            send(c, resp.c_str(), (int)resp.size(), 0);
#else
            send(c, resp.c_str(), resp.size(), MSG_NOSIGNAL);
#endif
        } else {
            const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
#ifdef _WIN32
            send(c, r, (int)strlen(r), 0);
#else
            send(c, r, strlen(r), MSG_NOSIGNAL);
#endif
        }
#ifdef _WIN32
        closesocket(c);
#else
        close(c);
#endif
    }
};
