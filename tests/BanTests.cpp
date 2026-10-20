#include "BanStore.hpp"
#include <cassert>
#include <cstdio>
int main() {
 std::remove("test-bans.db");
 {BanStore b("test-bans.db"); b.set("name","Nene","test reason"); b.set("ip","127.0.0.1","local"); assert(b.reason("name","NENE")=="test reason");}
 {BanStore b("test-bans.db"); assert(b.reason("ip","127.0.0.1")=="local"); assert(b.remove("name","nene")); assert(b.reason("name","Nene").empty());}
 {BanStore b("test-bans.db"); assert(b.reason("name","nene").empty());}
 std::remove("test-bans.db");
}
