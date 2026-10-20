#pragma once
#include "JsonText.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

struct LoggerOptions {
    std::string directory = "logs";
    bool packetLogging = true;
    bool outputToConsole=true,outputToFile=true;
    std::string consoleFormat="PlainText",fileFormat="PlainText";
    int logMaxMB = 8;
    int packetLogMaxMB = 16;
    int backups = 5;
    int packetHexBytes = 48;
    std::size_t maxQueue = 50000;
};

enum class LogLevel { Info, Warn, Error, Fatal };

class Logger {
public:
    static Logger& instance() {
        static Logger value;
        return value;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void configure(const LoggerOptions& options) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            options_ = options;
            if (options_.directory.empty()) options_.directory = "logs";
            options_.logMaxMB = std::clamp(options_.logMaxMB, 1, 1024);
            options_.packetLogMaxMB = std::clamp(options_.packetLogMaxMB, 1, 2048);
            options_.backups = std::clamp(options_.backups, 1, 20);
            options_.packetHexBytes = std::clamp(options_.packetHexBytes, 0, 256);
            options_.maxQueue = std::max<std::size_t>(options_.maxQueue, 1000);
            if (!worker_.joinable()) {
                running_ = true;
                worker_ = std::thread([this] { workerLoop(); });
            }
        }
        condition_.notify_one();
    }

    void prepareConsole() {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != INVALID_HANDLE_VALUE && out != nullptr) {
            DWORD mode = 0;
            if (GetConsoleMode(out, &mode))
                SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
        SetConsoleTitleW(L"REXT-PROXY | ONLINE | Terraria 1.4.5.8 / Protocol 326");
#endif
    }

    void showBanner() {
        // Minimal red terminal / hacker-console identity. No large framed ASCII art:
        // the live status and startup diagnostics remain readable in narrow terminals.
        static const char* lines[] = {
            "",
            "   ____  _______  _______ ",
            "  |  _ \\| ____\\ \\/ /_   _|",
            "  | |_) |  _|  \\  /  | |  ",
            "  |  _ <| |___ /  \\  | |  ",
            "  |_| \\_\\_____/_/\\_\\ |_|  ",
            "   PROXY / 1.4.5.8 / 326",
            ""
        };
        const char* red = "\x1b[91m";
        const char* darkRed = "\x1b[31m";
        const char* white = "\x1b[97m";
        const char* green = "\x1b[92m";
        const char* dim = "\x1b[90m";
        const char* reset = "\x1b[0m";
        for (std::size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            const char* color = dim;
            if (i == 1 || i == 8) color = red;
            else if (i == 2 || i == 9) color = darkRed;
            else if (i == 7) color = green;
            else if (i >= 3 && i <= 6) color = white;
            std::fputs(color, stdout);
            std::fputs(lines[i], stdout);
            std::fputs(reset, stdout);
            std::fputc('\n', stdout);
        }
        std::fflush(stdout);
    }

    void redirectStreams() {
        if (redirected_) return;
        oldCout_ = std::cout.rdbuf(&coutBuffer_);
        oldCerr_ = std::cerr.rdbuf(&cerrBuffer_);
        redirected_ = true;
    }

    void restoreStreams() {
        if (!redirected_) return;
        std::cout.flush();
        std::cerr.flush();
        coutBuffer_.flushCurrentThread();
        cerrBuffer_.flushCurrentThread();
        std::cout.rdbuf(oldCout_);
        std::cerr.rdbuf(oldCerr_);
        redirected_ = false;
    }

    void packet(const std::string& direction, int sessionId, std::uint64_t sequence,
                std::uint8_t packetId, const std::string& packetName,
                const std::vector<std::uint8_t>& frame) {
        LoggerOptions options;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ || !options_.packetLogging) return;
            options = options_;
        }
        std::ostringstream line;
        line << direction << '[' << sessionId << "] #" << sequence << ' '
             << packetName << '(' << static_cast<unsigned int>(packetId) << ") len="
             << frame.size() << " hex=" << std::hex << std::setfill('0');
        const std::size_t count = std::min<std::size_t>(frame.size(), static_cast<std::size_t>(options.packetHexBytes));
        for (std::size_t i = 0; i < count; ++i) {
            if (i) line << ' ';
            line << std::setw(2) << static_cast<unsigned int>(frame[i]);
        }
        if (frame.size() > count) line << " ... (" << std::dec << (frame.size() - count) << " more bytes)";
        enqueue(Entry{std::chrono::system_clock::now(), line.str(), LogLevel::Info, true, true});
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!worker_.joinable()) return;
            running_ = false;
        }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    ~Logger() {
        restoreStreams();
        stop();
    }

private:
    struct Entry {
        std::chrono::system_clock::time_point time;
        std::string text;
        LogLevel level = LogLevel::Info;
        bool errorStream = false;
        bool packet = false;
    };

    class CaptureBuffer final : public std::streambuf {
    public:
        CaptureBuffer(Logger* owner, bool errorStream) : owner_(owner), errorStream_(errorStream) {}

        void flushCurrentThread() {
            std::string pending;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = pending_.find(std::this_thread::get_id());
                if (it != pending_.end() && !it->second.empty()) {
                    pending = std::move(it->second);
                    pending_.erase(it);
                }
            }
            if (!pending.empty()) owner_->captureLine(std::move(pending), errorStream_);
        }

    protected:
        int_type overflow(int_type ch) override {
            if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
            const char c = traits_type::to_char_type(ch);
            append(&c, 1);
            return ch;
        }

        std::streamsize xsputn(const char* s, std::streamsize count) override {
            if (count > 0) append(s, static_cast<std::size_t>(count));
            return count;
        }

        int sync() override {
            flushCurrentThread();
            return 0;
        }

    private:
        Logger* owner_;
        bool errorStream_;
        std::mutex mutex_;
        std::unordered_map<std::thread::id, std::string> pending_;

        void append(const char* data, std::size_t count) {
            std::vector<std::string> lines;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto& buf = pending_[std::this_thread::get_id()];
                for (std::size_t i = 0; i < count; ++i) {
                    char c = data[i];
                    if (c == '\n') {
                        if (!buf.empty() && buf.back() == '\r') buf.pop_back();
                        lines.push_back(std::move(buf));
                        buf.clear();
                    } else {
                        buf.push_back(c);
                    }
                }
            }
            for (auto& line : lines) owner_->captureLine(std::move(line), errorStream_);
        }
    };

    Logger() : coutBuffer_(this, false), cerrBuffer_(this, true) {}

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Entry> queue_;
    LoggerOptions options_;
    bool running_ = false;
    std::size_t droppedEntries_ = 0;
    std::thread worker_;

    CaptureBuffer coutBuffer_;
    CaptureBuffer cerrBuffer_;
    std::streambuf* oldCout_ = nullptr;
    std::streambuf* oldCerr_ = nullptr;
    bool redirected_ = false;

    std::ofstream appFile_;
    std::ofstream packetFile_;
    std::filesystem::path appPath_;
    std::filesystem::path packetPath_;
    std::uint64_t appBytes_ = 0;
    std::uint64_t packetBytes_ = 0;
    std::size_t flushCounter_ = 0;

    static std::tm localTime(std::time_t t) {
        std::tm value{};
#ifdef _WIN32
        localtime_s(&value, &t);
#else
        localtime_r(&t, &value);
#endif
        return value;
    }

    static std::string dateStamp(std::chrono::system_clock::time_point tp) {
        const auto tt = std::chrono::system_clock::to_time_t(tp);
        const auto tm = localTime(tt);
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d");
        return out.str();
    }

    static std::string timestamp(std::chrono::system_clock::time_point tp, bool console) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
        const auto tt = std::chrono::system_clock::to_time_t(tp);
        const auto tm = localTime(tt);
        std::ostringstream out;
        if (console) out << std::put_time(&tm, "%H:%M:%S");
        else out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        out << '.' << std::setw(3) << std::setfill('0') << ms.count();
        return out.str();
    }

    static std::string lower(std::string value) {
        for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return value;
    }

    static LogLevel detectLevel(const std::string& text, bool errorStream) {
        const auto v = lower(text);
        if (v.find("[fatal]") != std::string::npos || v.find("fatal:") != std::string::npos) return LogLevel::Fatal;
        if (errorStream || v.find("exception") != std::string::npos || v.find("failed") != std::string::npos ||
            v.find("[error]") != std::string::npos || v.find("error:") != std::string::npos) return LogLevel::Error;
        if (v.find("[warn]") != std::string::npos || v.find("[!] ") != std::string::npos ||
            v.find("offline") != std::string::npos || v.find("timeout") != std::string::npos) return LogLevel::Warn;
        return LogLevel::Info;
    }

    void captureLine(std::string line, bool errorStream) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) return;
        enqueue(Entry{std::chrono::system_clock::now(), std::move(line), LogLevel::Info, errorStream, false});
    }

    void enqueue(Entry entry) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                // During very early start/late shutdown, preserve messages to the console.
                std::fwrite(entry.text.data(), 1, entry.text.size(), entry.packet ? stdout : (entry.errorStream ? stderr : stdout));
                std::fputc('\n', entry.packet ? stdout : (entry.errorStream ? stderr : stdout));
                return;
            }
            if (queue_.size() >= options_.maxQueue) {
                if (entry.packet) { ++droppedEntries_; return; }
                auto packetIt = std::find_if(queue_.begin(), queue_.end(), [](const Entry& item) { return item.packet; });
                if (packetIt != queue_.end()) queue_.erase(packetIt);
                else { ++droppedEntries_; return; }
                ++droppedEntries_;
            }
            queue_.push_back(std::move(entry));
        }
        condition_.notify_one();
    }

    static std::string levelName(LogLevel level) {
        switch (level) {
            case LogLevel::Info: return "INFO";
            case LogLevel::Warn: return "WARN";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
        }
        return "INFO";
    }

    static const char* levelColor(LogLevel level) {
        switch (level) {
            case LogLevel::Info: return "\x1b[92m";
            case LogLevel::Warn: return "\x1b[93m";
            case LogLevel::Error: return "\x1b[91m";
            case LogLevel::Fatal: return "\x1b[1;91m";
        }
        return "\x1b[0m";
    }

    static void writeConsole(const Entry& entry) {
        const LogLevel level = entry.packet ? LogLevel::Info : detectLevel(entry.text, entry.errorStream);
        const std::string ts = timestamp(entry.time, true);
        const std::string name = levelName(level);
        const char* reset = "\x1b[0m";
        std::fputs("\x1b[90m[", stdout);
        std::fputs(ts.c_str(), stdout);
        std::fputs("] ", stdout);
        std::fputs(levelColor(level), stdout);
        std::fputc('[', stdout);
        std::fputs(name.c_str(), stdout);
        std::fputs("] ", stdout);
        std::fputs(reset, stdout);
        if (entry.errorStream) std::fputs("\x1b[91m", stdout);
        std::fwrite(entry.text.data(), 1, entry.text.size(), stdout);
        std::fputs(reset, stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    static std::string jsonLogLine(const Entry& entry){
        return "{\"time\":"+jsonQuote(timestamp(entry.time,false))+",\"level\":"+
          jsonQuote(levelName(detectLevel(entry.text,entry.errorStream)))+",\"message\":"+jsonQuote(entry.text)+"}\n";
    }
    static void outputConsole(const Entry& entry,const LoggerOptions& options){
        if(!options.outputToConsole)return;
        if(options.consoleFormat=="JSON"){auto line=jsonLogLine(entry);std::fwrite(line.data(),1,line.size(),stdout);std::fflush(stdout);}
        else writeConsole(entry);
    }
    static std::string fullLogLine(const Entry& entry) {
        std::ostringstream out;
        out << '[' << timestamp(entry.time, false) << "] [" << levelName(detectLevel(entry.text, entry.errorStream)) << "] ";
        out << entry.text << '\n';
        return out.str();
    }

    static std::uint64_t fileSize(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return 0;
        auto value = std::filesystem::file_size(path, ec);
        return ec ? 0 : static_cast<std::uint64_t>(value);
    }

    void rotateFile(const std::filesystem::path& path, int backups) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        for (int i = backups; i >= 1; --i) {
            auto from = path;
            from += "." + std::to_string(i);
            if (i == backups) std::filesystem::remove(from, ec);
            ec.clear();
            if (i > 1) {
                auto previous = path;
                previous += "." + std::to_string(i - 1);
                if (std::filesystem::exists(previous, ec)) {
                    ec.clear();
                    std::filesystem::rename(previous, from, ec);
                }
            }
        }
        auto first = path;
        first += ".1";
        ec.clear();
        std::filesystem::rename(path, first, ec);
    }

    void ensureFile(std::ofstream& stream, std::filesystem::path& currentPath,
                    std::uint64_t& currentBytes, const std::filesystem::path& target,
                    std::uint64_t maxBytes, int backups) {
        if (stream.is_open() && currentPath == target && currentBytes < maxBytes) return;
        if (stream.is_open()) stream.close();
        currentPath = target;
        currentBytes = fileSize(target);
        if (currentBytes >= maxBytes) {
            rotateFile(target, backups);
            currentBytes = 0;
        }
        stream.open(target, std::ios::out | std::ios::app | std::ios::binary);
        if (!stream) {
            currentPath.clear();
            currentBytes = 0;
        } else {
            currentBytes = fileSize(target);
        }
    }

    void writeEntry(const Entry& entry, const LoggerOptions& options) {
        if(!options.outputToFile)return;
        std::error_code ec;
        std::filesystem::path dir(options.directory);
        std::filesystem::create_directories(dir, ec);
        const std::string day = dateStamp(entry.time);
        const std::string content = options.fileFormat=="JSON" ? jsonLogLine(entry) : entry.packet
            ? ('[' + timestamp(entry.time, false) + "] " + entry.text + "\n")
            : fullLogLine(entry);

        if (entry.packet) {
            if (!options.packetLogging) return;
            auto target = dir / ("packets-" + day + ".log");
            ensureFile(packetFile_, packetPath_, packetBytes_, target,
                       static_cast<std::uint64_t>(options.packetLogMaxMB) * 1024 * 1024,
                       options.backups);
            if (packetFile_.is_open()) {
                packetFile_.write(content.data(), static_cast<std::streamsize>(content.size()));
                packetBytes_ += content.size();
            }
        } else {
            auto target = dir / ("dimensions-" + day + ".log");
            ensureFile(appFile_, appPath_, appBytes_, target,
                       static_cast<std::uint64_t>(options.logMaxMB) * 1024 * 1024,
                       options.backups);
            if (appFile_.is_open()) {
                appFile_.write(content.data(), static_cast<std::streamsize>(content.size()));
                appBytes_ += content.size();
            }
        }
        if (++flushCounter_ >= 64) {
            if (appFile_.is_open()) appFile_.flush();
            if (packetFile_.is_open()) packetFile_.flush();
            flushCounter_ = 0;
        }
    }

    void workerLoop() {
        for (;;) {
            Entry entry;
            LoggerOptions options;
            std::size_t dropped = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait_for(lock, std::chrono::milliseconds(50), [this] { return !queue_.empty() || !running_; });
                if (queue_.empty() && !running_) break;
                if (queue_.empty()) {
                    options = options_;
                    dropped = droppedEntries_;
                    droppedEntries_ = 0;
                } else {
                    entry = std::move(queue_.front());
                    queue_.pop_front();
                    options = options_;
                    dropped = droppedEntries_;
                    droppedEntries_ = 0;
                }
            }
            if (dropped > 0) {
                Entry notice{std::chrono::system_clock::now(),
                    "[LOGGER] Warning: dropped " + std::to_string(dropped) + " queued log entries because the queue was full.",
                    LogLevel::Warn, false, false};
                outputConsole(notice,options);
                writeEntry(notice, options);
            }
            if (entry.text.empty()) {
                // Keep recent events durable even during long, quiet sessions.
                if (appFile_.is_open()) appFile_.flush();
                if (packetFile_.is_open()) packetFile_.flush();
                continue;
            }
            if (!entry.packet) outputConsole(entry,options);
            writeEntry(entry, options);
        }
        if (appFile_.is_open()) { appFile_.flush(); appFile_.close(); }
        if (packetFile_.is_open()) { packetFile_.flush(); packetFile_.close(); }
    }
};
