#include "ProxyServer.hpp" // Includes Winsock headers before Windows.h on Windows builds.
#include "Logger.hpp"
#include "Shutdown.hpp"

#include <iostream>

int main(int argc, char** argv) {
    installProxySignals();
    auto& logger = Logger::instance();
    logger.prepareConsole();
    logger.configure(LoggerOptions{});
    logger.showBanner();
    logger.redirectStreams();

    const std::string configPath = argc > 1 ? argv[1] : (std::filesystem::exists("config.yaml") ? "config.yaml" : "config.json");
    Config cfg;
    if (!loadConfig(configPath, cfg)) {
        std::cerr << "[FATAL] Failed to load config: " << configPath << "\n";
        std::cerr << "[FATAL] Check the selected JSON/YAML config and backend server definitions.\n";
        logger.restoreStreams();
        logger.stop();
        return 1;
    }

    LoggerOptions logOptions;
    logOptions.outputToConsole=cfg.logConsole;logOptions.outputToFile=cfg.logFile;
    logOptions.consoleFormat=cfg.logConsoleFormat;logOptions.fileFormat=cfg.logFileFormat;
    logOptions.directory = cfg.logDirectory;
    logOptions.packetLogging = cfg.packetLogging;
    logOptions.logMaxMB = cfg.logMaxMB;
    logOptions.packetLogMaxMB = cfg.packetLogMaxMB;
    logOptions.backups = cfg.logBackups;
    logOptions.packetHexBytes = cfg.packetHexBytes;
    logger.configure(logOptions);

    std::cout << "[BOOT] Config loaded: " << configPath << "\n";
    std::cout << "[BOOT] Terraria client protocol string: " << cfg.terrariaVersion << "\n";
    std::cout << "[BOOT] Packet trace logging: " << (cfg.packetLogging ? "enabled" : "disabled") << "\n";
    std::cout << "[BOOT] App logs: " << cfg.logDirectory << "/dimensions-YYYY-MM-DD.log\n";
    if (cfg.packetLogging)
        std::cout << "[BOOT] Packet logs: " << cfg.logDirectory << "/packets-YYYY-MM-DD.log\n";

    {
        ProxyServer proxy(std::move(cfg));
        proxy.start();
    }

    std::cout << "[BOOT] Proxy stopped cleanly.\n";
    logger.restoreStreams();
    logger.stop();
    return 0;
}
