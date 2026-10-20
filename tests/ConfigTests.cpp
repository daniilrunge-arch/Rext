#include "Config.hpp"
#include <cassert>
#include <cstdio>
int main(int argc,char**argv){
 assert(argc==2);Config c;assert(loadConfig(argv[1],c));
 assert(c.listeners.size()==2 && c.servers.size()==6);
 assert(c.listeners[1].port==8989 && c.servers[5].name=="cctg");
 assert(c.restPort==56698 && c.tcpRttEnabled);
 assert(c.phrase("availableDimensions").find("Доступные")!=std::string::npos);
 assert((c.debuffIds==std::vector<int>{163,47,149}));
 auto file=std::string("config-test-tmp.yaml");
 {std::ofstream f(file);f<<"servers:\n  - listenPort: 12345\n    routingServers:\n      - name: test\n        serverIP: 127.0.0.1\n        serverPort: 12346\noptions:\n  language: english\n  languageOverrides:\n    currentTcpRtt: 'RTT ${overallRtt}'\n  debuffOnSwitch:\n    enabled: true\n    buffTypes: [47]\n    debuffTimeInSeconds: 2\n";}
 Config en;assert(loadConfig(file,en));assert(en.phrase("availableDimensions").find("Available")!=std::string::npos);assert(en.phrase("currentTcpRtt")=="RTT ${overallRtt}");assert(en.debuffIds.size()==1 && en.debuffIds[0]==47 && en.debuffSeconds==2);
 {std::ofstream f(file);f<<"servers:\n  - listenPort: 99999\n";}
 Config bad;assert(!loadConfig(file,bad));std::remove(file.c_str());
}
