# REXT-PROXY

A C++17 multi-server proxy for **Terraria 1.4.5.8 (protocol 326)**. Players connect to one address and switch between lobby, survival and build servers using chat commands.

English-only configuration and language-header edition.

## Requirements

- A C++17 compiler: MinGW-w64 GCC on Windows, or GCC on Linux.
- Separate Terraria-compatible backend servers, such as TShock or TLS.
- A compatible Dimensions/RealIP adapter on each backend that receives control packets.

The proxy does not replace a game server. .NET is not required to build the proxy itself; the TShock adapter is a separate C# project.

## Repository files

Keep `.cpp` and `.hpp` files in the project root. Include the complete `extensions/` and `tests/` directories from the source bundle. The current CMake configuration requires `extensions/PlayTime/PlayTimeExtension.cpp` and enables tests by default.

This documentation pack supplements the source repository; it does not contain the full proxy source or backend adapters.

## Build and run

### Windows

Install MinGW-w64 GCC, add its compiler directory to PATH, and verify `g++ --version` in a new command prompt. From the project directory:

```bat
build.bat
dimensions.exe config.yaml
```

The build creates `dimensions.exe` and the PlayTime extension. `run.bat` automatically uses `config.yaml` when it is present.

### Linux

```bash
sh build.sh
./dimensions config.yaml
```

### CMake

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use `-DBUILD_TESTING=OFF` in the first command to disable tests. PlayTime source is still required. Start the generated executable with the full path to your chosen configuration if running from another directory.

## Configuration and language

- `config.yaml`: English configuration with editable English messages.
- `config.json`: English legacy configuration.
- `Language.hpp`: English-only defaults, including calls that previously selected Russian.

Replace these files in the project root and rebuild the proxy. Changing the header requires rebuilding; changing YAML alone does not. Both boolean variants of `languageDefaults` now return English while keeping the existing C++ function signature.

Use YAML for RealIP adapters. The legacy JSON loader does not read `isVanilla` and leaves backends in vanilla mode. JSON and YAML settings are not merged. Without an explicit filename, the proxy prefers `config.yaml`.

Edit messages under `options.languageOverrides`. Preserve `${name}`, `${total}`, `${overallRtt}` and Terraria chat tags. The legacy JSON switch message uses `{server}` instead.

This pack translates Language.hpp and both configuration files. Hardcoded console, administrative and extension messages in other source files are outside this pack and may remain Russian.

### Backend example

The example listens for players on proxy port **7777** and uses these local backends:

| Name | Backend address | Backend port | Chat command |
| --- | --- | --- | --- |
| lobby | 127.0.0.1 | 7778 | `/lobby` |
| survival | 127.0.0.1 | 7779 | `/survival` |
| build | 127.0.0.1 | 7780 | `/build` |

Replace the addresses and ports with your own. `127.0.0.1` works only when the backend is reachable in the proxy's local network namespace. Players connect to the proxy address and its `listenPort`, not these backend ports.

Each backend needs a unique `name`. A group without `listenPort` provides switch destinations without creating another entry port. If an entry group contains multiple servers, routing uses the active connection counts known to this proxy process.

`hidden: true` removes a server from the list but does not prevent switching to it by name. `isVanilla: false` enables adapter control packets; use `true` for servers without a compatible adapter.

Keep lobby first if it should receive players returning after a backend kick. `disconnectOnKick.type: never` enables that return behavior; `always` disconnects the player instead. A kick from the fallback server cannot return the player to that same server.

## Player commands

| Command | Purpose |
| --- | --- |
| `/dim`, `/dimensions` | List available servers |
| `/lobby`, `/survival`, `/build` | Switch to the named backend |
| `/join survival` | Switch by name |
| `/ping` | Show player-to-proxy TCP RTT |
| `/void` | Detach from the backend while keeping the loaded world |
| `/back` | Return from the Void |
| `/phelp` | Show proxy command help |

The Void does not generate a new empty world. Ping does not include backend latency. The default colors are green up to 80 ms, yellow up to 150 ms, and red above that. Native RTT may be unavailable on unsupported platforms.

## RealIP adapters

Set `isVanilla: false` for adapted backends and configure each backend to trust the physical IP used by your proxy connection. Behind NAT, use the proxy's outgoing NAT address. Do not add player addresses to the trusted-proxy list.

For TLS 1.3.2 from the v4.4 bundle, the core receiver in `src/dimensions.ts` and the updated `src/client.ts` are required. Replacing only an extension's `index.js` is insufficient. Run `npm run build` after changing sources. Add this top-level field to TLS `config/config.json`:

```json
"dimensions": {
  "trustedProxyIPs": ["127.0.0.1", "::1", "203.0.113.10"]
}
```

**203.0.113.10 is a placeholder. Replace it with your proxy's physical IP, or remove it for a local-only setup.**

For the bundled TShock adapter, build the separate `Dimensions-TShock` project with .NET 9 for TShock 6.2.1. Replace the old `ServerPlugins/Dimensions.dll`; do not load it alongside another RealIP receiver. Configure `tshock/Dimensions.json`:

```json
{
  "EnableGeoIP": false,
  "TrustedProxyIPs": ["127.0.0.1", "::1", "203.0.113.10"]
}
```

Replace the placeholder here too. GeoIP lookup is not implemented by this receiver. This documentation pack does not include a compiled plugin.

In the proxy console, use `ip PLAYER_NAME`: `applied=YES` means the backend acknowledged application; `UNCONFIRMED` means it has not. Repeat after switching servers. Check adapter versions, trusted peers and `isVanilla` if no acknowledgement arrives.

RealIP updates TLS `client.ip` or TShock `TSPlayer.IP`. The actual TCP socket still belongs to the proxy. Plugins reading the socket directly will still see the proxy address.

## Shutdown and updates

Use `exit` or Ctrl+C for graceful shutdown. The proxy sends the text from `languageOverrides.close` to connected players. A process manager or panel must restart the process; the command itself does not restart it. Forced termination cannot send a final message.

Preserve your configurations, `bans.db`, `warps.db` and `player_rules.db` when updating. Backend servers remain responsible for SSC and inventory separation.

## Limitations and validation

Redis, hot reload and the RTT REST endpoint are not implemented. The YAML parser supports the subset used in the examples, without anchors or multiline scalars. REST is disabled in the YAML examples.

Configuration files are checked using the proxy's own loader. Local protocol tests do not establish live-client compatibility on your servers. Validate switching, inventories and adapters with your Terraria client before opening the network to players.
