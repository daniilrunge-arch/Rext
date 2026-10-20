#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/select.h>
#endif
// Polling input allows shutdown without a blocked std::cin/CRT input lock.
inline bool readConsoleLine(std::string& line,const std::atomic<bool>& alive){
 line.clear();
 while(alive.load()){
#ifdef _WIN32
  HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD mode=0;char c=0;
  if(GetConsoleMode(input,&mode)){
   DWORD events=0;if(!GetNumberOfConsoleInputEvents(input,&events))return false;
   if(!events){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
   INPUT_RECORD record{};DWORD got=0;if(!ReadConsoleInputW(input,&record,1,&got))return false;
   if(!got||record.EventType!=KEY_EVENT||!record.Event.KeyEvent.bKeyDown)continue;
   wchar_t wc=record.Event.KeyEvent.uChar.UnicodeChar;
   if(!wc)continue;
   DWORD written=0;HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
   if(wc==L'\r'){WriteConsoleW(output,L"\r\n",2,&written,nullptr);return true;}
   if(wc==L'\b'){
    if(!line.empty()){do {auto b=static_cast<unsigned char>(line.back());line.pop_back();if((b&0xc0)!=0x80)break;}while(!line.empty());WriteConsoleW(output,L"\b \b",3,&written,nullptr);}continue;
   }
   if(wc<32)continue;
   char utf8[8];int count=WideCharToMultiByte(CP_UTF8,0,&wc,1,utf8,8,nullptr,nullptr);
   if(count>0){line.append(utf8,count);WriteConsoleW(output,&wc,1,&written,nullptr);}continue;
  }
  if(GetFileType(input)==FILE_TYPE_PIPE){DWORD available=0;if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr))return false;if(!available){std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}}
  DWORD got=0;if(!ReadFile(input,&c,1,&got,nullptr)||!got)return false;
#else
  fd_set fds;FD_ZERO(&fds);FD_SET(STDIN_FILENO,&fds);timeval delay{0,100000};
  if(select(STDIN_FILENO+1,&fds,nullptr,nullptr,&delay)<=0)continue;
  char c;if(read(STDIN_FILENO,&c,1)!=1)return false;
#endif
  if(c=='\n')return true;
  if(c!='\r' && line.size()<65536)line+=c;
 }
 return false;
}
