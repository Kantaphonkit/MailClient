// Logger.h
// Appends timestamped lines to logs/client.log, e.g.
//   2026-10-08 14:10:02 [INFO] Connected to SMTP server 127.0.0.1:25
// Rule: never pass a password to the logger.
#pragma once

#include <string>

class Logger {
public:
    static void init(const std::string& path = "logs/client.log");
    static void info(const std::string& msg);
    static void error(const std::string& msg);

private:
    static void write(const char* level, const std::string& msg);
};