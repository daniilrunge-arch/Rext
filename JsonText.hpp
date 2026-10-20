#pragma once
#include <string>
inline std::string jsonQuote(const std::string& value){
 std::string out="\"";const char* hex="0123456789abcdef";
 for(unsigned char c:value){
  if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
  else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
  else out+=char(c);
 }
 return out+'"';
}
