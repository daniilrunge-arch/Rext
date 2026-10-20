#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
mkdir -p extensions
g++ -std=c++17 -O0 -pthread -rdynamic main.cpp ProxyServer.cpp WarpManager.cpp CommandHandler.cpp PlayerRules.cpp -ldl -o dimensions
g++ -std=c++17 -O0 -fPIC -shared extensions/PlayTime/PlayTimeExtension.cpp -o extensions/PlayTime.so
printf 'Built dimensions + extensions/PlayTime.so\n'
