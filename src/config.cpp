// Config.cpp
#include "Config.h"

#include <cctype>
#include <fstream>

#include "MailMessage.h"

namespace {

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    int toInt(const std::string& value, const std::string& key, int minValue, int maxValue) {
        size_t used = 0;
        int n = 0;
        try {
            n = std::stoi(value, &used);
        }
        catch (...) {
            used = 0;
        }
        if (used == 0 || used != value.size() || n < minValue || n > maxValue) {
            throw ConfigError("Invalid value for '" + key + "': '" + value + "' (expected a number " +
                std::to_string(minValue) + "-" + std::to_string(maxValue) + ")");
        }
        return n;
    }

}  // namespace

Config Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw ConfigError("Cannot open config file '" + path + "'");
    }

    Config cfg;
    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;  // blank line or comment

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            throw ConfigError(path + " line " + std::to_string(lineNo) + ": expected key = value");
        }
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "server") {
            cfg.server = value;
        }
        else if (key == "smtp_port") {
            cfg.smtpPort = toInt(value, key, 1, 65535);
        }
        else if (key == "pop3_port") {
            cfg.pop3Port = toInt(value, key, 1, 65535);
        }
        else if (key == "username") {
            cfg.username = value;
        }
        else if (key == "email") {
            cfg.email = value;
        }
        else if (key == "timeout_seconds") {
            cfg.timeoutSeconds = toInt(value, key, 1, 300);
        }
        else if (key == "mailbox_dir") {
            cfg.mailboxDir = value;
        }
        else if (key == "log_file") {
            cfg.logFile = value;
        }
        else if (key == "password") {
            throw ConfigError("Do not put the password in the config file; "
                "the client asks for it when needed.");
        }
        else {
            throw ConfigError(path + " line " + std::to_string(lineNo) + ": unknown key '" + key + "'");
        }
    }

    // Validate what the rest of the program relies on.
    if (cfg.server.empty()) throw ConfigError("'server' must not be empty");
    if (cfg.username.empty()) throw ConfigError("'username' is required");
    if (cfg.email.empty()) cfg.email = cfg.username;
    if (!MailMessage::isValidAddress(cfg.email)) {
        throw ConfigError("'email' is not a valid address: '" + cfg.email + "'");
    }
    return cfg;
}