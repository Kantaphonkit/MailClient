// Config.h
// Settings read from a simple "key = value" text file (config/client.conf).
// The password is NOT stored here: it is asked for at runtime and never written to disk.
#pragma once

#include <stdexcept>
#include <string>

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string server = "127.0.0.1";  // mail server address (SMTP and POP3)
    int smtpPort = 25;
    int pop3Port = 110;
    std::string username;              // POP3 login, e.g. User_2@test.local
    std::string email;                 // sender address; defaults to username
    int timeoutSeconds = 10;
    std::string mailboxDir = "data/mailbox";
    std::string logFile = "logs/client.log";

    // Read and validate the file. Throws ConfigError with a clear message.
    static Config load(const std::string& path);
};