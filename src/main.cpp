// main.cpp — program entry point: load settings, start networking, run the menu.
#include <exception>
#include <iostream>

#include "Cli.h"
#include "Config.h"
#include "Logger.h"
#include "MailService.h"
#include "TcpConnection.h"

int main() {
    try {
        Config cfg = Config::load("config/client.conf");
        Logger::init(cfg.logFile);
        Logger::info("Client started");

        SocketSystem sockets;     // WSAStartup / WSACleanup for the whole run
        MailService service(cfg);
        Cli cli(service);
        cli.run();

        Logger::info("Client exited");
    }
    catch (const ConfigError& e) {
        std::cerr << "Config error: " << e.what() << "\n";
        return 1;
    }
    catch (const std::exception& e) {
        // Last safety net, e.g. the mailbox folder cannot be created.
        Logger::error(std::string("Fatal: ") + e.what());
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}