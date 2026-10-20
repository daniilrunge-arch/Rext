#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cctype>
// Portable configuration YAML subset: indented maps/sequences, quoted/plain
// scalars, inline scalar lists and {}. Reject unsupported syntax explicitly.
namespace yaml {
inline std::string trim(std::string s) {auto a=s.find_first_not_of(" \r\n");if(a==s.npos)return {};return s.substr(a,s.find_last_not_of(" \r\n")-a+1);}
inline size_t separator(const std::string& s,char ch) {
 char quote=0;bool escape=false;
 for(size_t i=0;i<s.size();++i){char c=s[i];if(escape){escape=false;continue;}if(quote=='"'&&c=='\\'){escape=true;continue;}if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c==ch)return i;}
 return s.npos;
}
inline std::string scalar(std::string s) {
 s=trim(s);if(s.empty())return s;
 if(s.front()=='"'||s.front()=='\'') {
  char q=s.front();if(s.size()<2||s.back()!=q)throw std::runtime_error("Unclosed YAML quote");
  std::string out;for(size_t i=1;i+1<s.size();++i){char c=s[i];if(q=='"'&&c=='\\'){c=s[++i];if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c!='"'&&c!='\\')throw std::runtime_error("Unsupported YAML escape; use UTF-8 text");}else if(q=='\''&&c=='\''&&i+2<s.size()&&s[i+1]=='\'')++i;out+=c;}return out;
 }
 if(s[0]=='&'||s[0]=='*'||s=="|"||s==">")throw std::runtime_error("YAML anchors/block strings unsupported");return s;
}
struct Node {
 std::string value;std::map<std::string,Node> map;std::vector<Node> list;
 const Node& operator[](const std::string& k)const{static Node empty;auto i=map.find(k);return i==map.end()?empty:i->second;}
 std::string str(std::string fallback={})const{return value.empty()?fallback:value;}
 int integer(int d)const{if(value.empty())return d;size_t used=0;int n=std::stoi(value,&used);if(used!=value.size())throw std::runtime_error("Invalid YAML integer: "+value);return n;}
 bool boolean(bool d)const{if(value.empty())return d;if(value=="true")return true;if(value=="false")return false;throw std::runtime_error("Invalid YAML boolean: "+value);}
};
inline Node inlineValue(std::string s){Node n;s=trim(s);if(s=="{}")return n;if(!s.empty()&&s.front()=='['){if(s.back()!=']')throw std::runtime_error("Unclosed list");s=s.substr(1,s.size()-2);while(!trim(s).empty()){auto i=separator(s,',');Node v;v.value=scalar(s.substr(0,i));n.list.push_back(v);if(i==s.npos)break;s=s.substr(i+1);}return n;}n.value=scalar(s);return n;}
struct Line{int indent;std::string text;};
class Parser {
 std::vector<Line> lines;size_t pos=0;
 void pair(Node& n,std::string text,int indent){auto colon=separator(text,':');if(colon==text.npos)throw std::runtime_error("Expected YAML key: value");auto key=scalar(text.substr(0,colon));if(n.map.count(key))throw std::runtime_error("Duplicate YAML key: "+key);auto value=trim(text.substr(colon+1));if(value.empty()&&pos<lines.size()&&lines[pos].indent>indent)n.map[key]=block(lines[pos].indent);else n.map[key]=inlineValue(value);}
 Node block(int indent){Node n;bool sequence=lines[pos].text.rfind("- ",0)==0;
  while(pos<lines.size()&&lines[pos].indent==indent){auto text=lines[pos++].text;
   if(sequence){if(text.rfind("- ",0)!=0)throw std::runtime_error("Mixed YAML map/list");Node child;auto rest=trim(text.substr(2));if(separator(rest,':')!=rest.npos){pair(child,rest,indent+2);while(pos<lines.size()&&lines[pos].indent==indent+2){auto t=lines[pos++].text;pair(child,t,indent+2);}}else child=inlineValue(rest);n.list.push_back(child);
   }else{if(text.rfind("- ",0)==0)throw std::runtime_error("Unexpected list");pair(n,text,indent);}}
  return n;
 }
public:
 Node read(const std::string& path){std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open "+path);std::string s;while(std::getline(f,s)){if(lines.empty()&&s.rfind("\xef\xbb\xbf",0)==0)s.erase(0,3);if(s.find('\t')!=s.npos)throw std::runtime_error("Use spaces, not tabs, in YAML");auto comment=separator(s,'#');if(comment!=s.npos)s.resize(comment);if(trim(s).empty())continue;int indent=int(s.find_first_not_of(' '));lines.push_back({indent,trim(s)});}if(lines.empty())throw std::runtime_error("Empty YAML");auto n=block(lines[0].indent);if(pos!=lines.size())throw std::runtime_error("Invalid YAML indentation");return n;}
};
}
