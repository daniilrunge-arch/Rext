#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <map>
#include <mutex>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <stdexcept>
#include <cctype>
class BanStore {
    std::mutex mutex;
    std::map<std::string,std::string> entries;
    std::string path;
    static std::string key(std::string s) { for(auto& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
    void save() {
        std::ofstream f(path+".tmp", std::ios::trunc);
        for (auto& e:entries) f << std::quoted(e.first) << ' ' << std::quoted(e.second) << '\n';
        f.close(); if(!f) throw std::runtime_error("Cannot save bans");
#ifdef _WIN32
        if (!MoveFileExA((path+".tmp").c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace bans file");
#else
        std::filesystem::rename(path+".tmp",path);
#endif
    }
public:
    explicit BanStore(std::string file="bans.db"):path(std::move(file)) {
        std::ifstream f(path); std::string k,r;
        while(f >> std::quoted(k) >> std::quoted(r)) entries[k]=r;
    }
    std::string reason(const std::string& kind,const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it=entries.find(kind+":"+key(value)); return it==entries.end()?"":it->second;
    }
    void set(const std::string& kind,const std::string& value,const std::string& reason) {
        std::lock_guard<std::mutex> lock(mutex); auto old=entries;
        entries[kind+":"+key(value)]=reason.empty()?"Заблокирован администратором.":reason;
        try {save();} catch(...) {entries=std::move(old); throw;}
    }
    bool remove(const std::string& kind,const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex); auto old=entries;
        if(!entries.erase(kind+":"+key(value))) return false;
        try {save();} catch(...) {entries=std::move(old); throw;} return true;
    }
    std::map<std::string,std::string> list() {std::lock_guard<std::mutex> lock(mutex); return entries;}
};
