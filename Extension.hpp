#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

class ProxyServer;
struct Session;

// Factory symbols have C linkage. The interface itself uses the C++ ABI.
// Build proxy and modules with the same compiler, architecture and headers.
inline constexpr int REXT_EXTENSION_API = 1;
using GetExtensionApiFn = int (*)();

// C ABI для экспорта из DLL/SO
extern "C" {
    struct ExtensionInfo {
        const char* name;
        const char* version;
    };
}

class IExtension {
public:
    virtual ~IExtension() = default;

    virtual const char* name() const = 0;
    virtual const char* version() const = 0;

    virtual void onLoad(ProxyServer* proxy) { (void)proxy; }

    // true = пакет обработан/заблокирован
    virtual bool onClientPacket(std::shared_ptr<Session> session,
                                const std::vector<uint8_t>& frame) {
        (void)session; (void)frame;
        return false;
    }

    virtual bool onServerPacket(std::shared_ptr<Session> session,
                                const std::vector<uint8_t>& frame) {
        (void)session; (void)frame;
        return false;
    }
};

using CreateExtensionFn  = IExtension* (*)();
using DestroyExtensionFn = void (*)(IExtension*);
using GetExtensionInfoFn = ExtensionInfo (*)();
