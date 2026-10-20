#pragma once
#include <iomanip>
#include <sstream>
#include <string>
inline std::string styledPing(double milliseconds,int greenMax,int yellowMax){
    const char* color=milliseconds<=greenMax?"8efa8e":milliseconds<=yellowMax?"ffdc64":"ff5555";
    int flag=milliseconds<=greenMax?4085:milliseconds<=yellowMax?4087:4084;
    std::ostringstream text;text<<"[i:"<<flag<<"][c/"<<color<<":"<<std::fixed<<std::setprecision(2)<<milliseconds<<" ms]";
    return text.str();
}
