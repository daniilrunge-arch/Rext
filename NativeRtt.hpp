#pragma once
#include "Session.hpp"
#include <optional>
#ifdef _WIN32
#include <mstcpip.h>
#endif
// Kernel TCP RTT on the existing connection; never synthesize a latency.
inline std::optional<double> nativeRttMs(socket_t sock) {
    if (sock == INVALID_SOCK) return {};
#ifdef _WIN32
#ifdef SIO_TCP_INFO
    DWORD version=0, bytes=0;
    TCP_INFO_v0 info{};
    if(WSAIoctl(sock,SIO_TCP_INFO,&version,sizeof(version),&info,sizeof(info),&bytes,nullptr,nullptr)==0)
        return info.RttUs / 1000.0;
#endif
#elif defined(__linux__)
    tcp_info info{}; socklen_t size=sizeof(info);
    if(getsockopt(sock,IPPROTO_TCP,TCP_INFO,&info,&size)==0) return info.tcpi_rtt/1000.0;
#endif
    return {};
}
