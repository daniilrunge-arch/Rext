#include "PingStyle.hpp"
#include <cassert>
int main(){
 assert(styledPing(10,80,150)=="[i:4085][c/8efa8e:10.00 ms]");
 assert(styledPing(80,80,150).find("4085")!=std::string::npos);
 assert(styledPing(80.01,80,150).find("4087")!=std::string::npos);
 assert(styledPing(150,80,150).find("ffdc64")!=std::string::npos);
 assert(styledPing(150.01,80,150).find("4084][c/ff5555")!=std::string::npos);
}
