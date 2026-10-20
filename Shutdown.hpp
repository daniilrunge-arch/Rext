#pragma once
#include <csignal>
inline volatile std::sig_atomic_t proxyStopRequested=0;
inline void proxySignalHandler(int){proxyStopRequested=1;}
#ifdef _WIN32
inline BOOL WINAPI proxyConsoleHandler(DWORD event){
 if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT){proxyStopRequested=1;return TRUE;}
 return FALSE;
}
#endif
inline void installProxySignals(){
 std::signal(SIGINT,proxySignalHandler);std::signal(SIGTERM,proxySignalHandler);
#ifdef _WIN32
 SetConsoleCtrlHandler(proxyConsoleHandler,TRUE);
#endif
}
