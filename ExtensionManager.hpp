#pragma once

#include "Extension.hpp"
#include "Session.hpp"

#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <filesystem>
#include <cstdint>
#include <mutex>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

class ProxyServer;

class ExtensionManager {
public:
    explicit ExtensionManager(ProxyServer* proxy)
        : proxy_(proxy), folder_("extensions") {}

    ~ExtensionManager() { unloadAll(); }

    void loadExtensions() {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(folder_, ec)) {
            fs::create_directories(folder_, ec);
            std::cout << "[EXT] Folder \"" << folder_ << "\" created.\n";
            return;
        }

        std::cout << "[EXT] Loading extensions from \"" << folder_ << "\"...\n";

        for (auto& entry : fs::directory_iterator(folder_, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;

            auto path = entry.path();
            auto ext = path.extension().string();
#ifdef _WIN32
            if (ext != ".dll" && ext != ".DLL") continue;
#else
            if (ext != ".so") continue;
#endif
            loadOne(path.string());
        }

        std::cout << "[EXT] Loaded " << loaded_.size() << " extension(s).\n";
    }

    bool handleClientPacket(std::shared_ptr<Session> session,
                            const std::vector<uint8_t>& frame) {
        std::lock_guard<std::mutex> guard(callbackMutex_);
        for (auto& e : loaded_) {
            if (!e.enabled) continue;
            try {
                if (e.instance && e.instance->onClientPacket(session, frame))
                    return true;
            } catch (...) {
                std::cerr << "[EXT] exception in " << e.name << " (client)\n";
            }
        }
        return false;
    }

    bool handleServerPacket(std::shared_ptr<Session> session,
                            const std::vector<uint8_t>& frame) {
        std::lock_guard<std::mutex> guard(callbackMutex_);
        for (auto& e : loaded_) {
            if (!e.enabled) continue;
            try {
                if (e.instance && e.instance->onServerPacket(session, frame))
                    return true;
            } catch (...) {
                std::cerr << "[EXT] exception in " << e.name << " (server)\n";
            }
        }
        return false;
    }

    void printList() {
        std::lock_guard<std::mutex> guard(callbackMutex_);
        std::cout << "[EXT] Расширений: " << loaded_.size() << "\n";
        for (const auto& e : loaded_) std::cout << "  " << (e.enabled ? "[ON]  " : "[OFF] ") << e.name << " v" << e.version << "\n";
    }
    bool setEnabled(std::string name, bool enabled) {
        std::lock_guard<std::mutex> guard(callbackMutex_);
        auto lower = [](std::string s) { for(auto& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s; };
        for(auto& e:loaded_) if(lower(e.name)==lower(name)) {
            e.enabled=enabled;
            std::cout << "[EXT] " << e.name << (enabled ? " включено" : " выключено") << " до перезапуска.\n";
            return true;
        }
        return false;
    }
private:
    std::mutex callbackMutex_;
    struct LoadedExt {
        bool enabled = true;
        std::string name;
        std::string version;
        std::string path;
#ifdef _WIN32
        HMODULE handle = nullptr;
#else
        void* handle = nullptr;
#endif
        IExtension* instance = nullptr;
        DestroyExtensionFn destroy = nullptr;
    };

    ProxyServer* proxy_;
    std::string folder_;
    std::vector<LoadedExt> loaded_;

    void unloadAll() {
        for (auto& e : loaded_) {
            if (e.instance && e.destroy) {
                e.destroy(e.instance);
                e.instance = nullptr;
            }
#ifdef _WIN32
            if (e.handle) FreeLibrary(e.handle);
#else
            if (e.handle) dlclose(e.handle);
#endif
        }
        loaded_.clear();
    }

    void loadOne(const std::string& path) {
#ifdef _WIN32
        HMODULE h = LoadLibraryA(path.c_str());
        if (!h) {
            std::cerr << "[EXT] Failed to load " << path
                      << " err=" << GetLastError() << "\n";
            return;
        }
        auto apiFn = reinterpret_cast<GetExtensionApiFn>(GetProcAddress(h, "get_extension_api"));
        auto create  = reinterpret_cast<CreateExtensionFn>(
            GetProcAddress(h, "create_extension"));
        auto destroy = reinterpret_cast<DestroyExtensionFn>(
            GetProcAddress(h, "destroy_extension"));
        auto infoFn  = reinterpret_cast<GetExtensionInfoFn>(
            GetProcAddress(h, "get_extension_info"));
#else
        void* h = dlopen(path.c_str(), RTLD_NOW);
        if (!h) {
            std::cerr << "[EXT] Failed to load " << path << ": " << dlerror() << "\n";
            return;
        }
        auto apiFn = reinterpret_cast<GetExtensionApiFn>(dlsym(h, "get_extension_api"));
        auto create  = reinterpret_cast<CreateExtensionFn>(dlsym(h, "create_extension"));
        auto destroy = reinterpret_cast<DestroyExtensionFn>(dlsym(h, "destroy_extension"));
        auto infoFn  = reinterpret_cast<GetExtensionInfoFn>(dlsym(h, "get_extension_info"));
#endif

        if (!create || !destroy || !apiFn || apiFn() != REXT_EXTENSION_API) {
            std::cerr << "[EXT] " << path
                      << " incompatible API or missing factory exports\n";
#ifdef _WIN32
            FreeLibrary(h);
#else
            dlclose(h);
#endif
            return;
        }

        IExtension* inst = nullptr;
        try { inst = create(); } catch (...) { std::cerr << "[EXT] Factory failed: " << path << "\n"; }
        if (!inst) {
            std::cerr << "[EXT] create_extension returned null: " << path << "\n";
#ifdef _WIN32
            FreeLibrary(h);
#else
            dlclose(h);
#endif
            return;
        }

        LoadedExt e;
        e.path = path;
        e.handle = h;
        e.instance = inst;
        e.destroy = destroy;
        e.name = inst->name() ? inst->name() : "Unknown";
        e.version = inst->version() ? inst->version() : "0.0.0";

        if (infoFn) {
            auto info = infoFn();
            if (info.name) e.name = info.name;
            if (info.version) e.version = info.version;
        }

        try {
            inst->onLoad(proxy_);
        } catch (...) {
            std::cerr << "[EXT] onLoad failed: " << e.name << "\n";
            destroy(inst);
#ifdef _WIN32
            FreeLibrary(h);
#else
            dlclose(h);
#endif
            return;
        }

        std::cout << "[Extension] " << e.name << " v" << e.version << " loaded.\n";
        loaded_.push_back(std::move(e));
    }
};
