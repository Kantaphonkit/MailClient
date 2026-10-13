// Pop3Client.cpp
#include "Pop3Client.h"

#include <iostream>
#include <sstream>
#include <utility>

#include "Logger.h"
#include "ProtocolErrors.h"

Pop3Client::Pop3Client(std::string host, int port, int timeoutMs)
    : host_(std::move(host)), port_(port), timeoutMs_(timeoutMs) {
}

// A POP3 status line is "+OK text" or "-ERR text".
std::string Pop3Client::readStatus() {
    std::string line = conn_.readLine();
    if (trace_) std::cout << "S: " << line << "\n";

    if (line.rfind("+OK", 0) == 0) {                 // starts with "+OK"
        return line.size() > 4 ? line.substr(4) : "";
    }
    if (line.rfind("-ERR", 0) == 0) {
        throw ProtocolError("Server error: " + (line.size() > 5 ? line.substr(5) : line));
    }
    throw ProtocolError("Malformed POP3 reply: '" + line + "'");
}

std::string Pop3Client::command(const std::string& line) {
    if (trace_) std::cout << "C: " << line << "\n";
    conn_.sendLine(line);
    return readStatus();
}

// Multi-line data (after "+OK" of LIST or RETR) ends with a line that is exactly ".".
// A data line that starts with "." was sent with an extra "." (dot-stuffing), so remove one.
std::vector<std::string> Pop3Client::readMultiline() {
    std::vector<std::string> lines;
    while (true) {
        std::string line = conn_.readLine();
        if (line == ".") break;                       // end of data
        if (!line.empty() && line[0] == '.') line.erase(0, 1);
        lines.push_back(std::move(line));
    }
    if (trace_) std::cout << "S: [" << lines.size() << " lines]\nS: .\n";
    return lines;
}

void Pop3Client::connect() {
    conn_.setReadTimeout(timeoutMs_);
    conn_.connect(host_, port_, 5000);
    Logger::info("Connected to POP3 server " + host_ + ":" + std::to_string(port_));
    try {
        readStatus();                                 // greeting: "+OK POP3"
    }
    catch (...) {
        conn_.close();
        throw;
    }
}

void Pop3Client::login(const std::string& user, const std::string& password) {
    try {
        command("USER " + user);

        // Send PASS ourselves so the password never appears in the trace or the log.
        if (trace_) std::cout << "C: PASS ********\n";
        conn_.sendLine("PASS " + password);
        readStatus();
    }
    catch (const ProtocolError& e) {
        // "-ERR" during USER/PASS means the login was rejected.
        Logger::error("POP3 authentication failed for " + user);
        conn_.close();
        throw AuthError(std::string("Login failed: ") + e.what());
    }
    Logger::info("POP3 login successful for " + user);
}

std::pair<int, std::size_t> Pop3Client::stat() {
    // "+OK 2 733"  ->  2 messages, 733 bytes in total
    std::istringstream in(command("STAT"));
    int count = 0;
    std::size_t bytes = 0;
    if (!(in >> count >> bytes)) throw ProtocolError("Malformed STAT reply");
    return { count, bytes };
}

std::vector<Pop3ListEntry> Pop3Client::list() {
    command("LIST");
    std::vector<Pop3ListEntry> entries;
    for (const std::string& line : readMultiline()) {
        // each line: "<number> <size>", e.g. "2 507"
        std::istringstream in(line);
        Pop3ListEntry e;
        if (!(in >> e.number >> e.size)) throw ProtocolError("Malformed LIST line: '" + line + "'");
        entries.push_back(e);
    }
    return entries;
}

std::string Pop3Client::retr(int number) {
    command("RETR " + std::to_string(number));
    std::string raw;
    for (const std::string& line : readMultiline()) {
        raw += line + "\r\n";
    }
    Logger::info("Retrieved message " + std::to_string(number) + " (" +
        std::to_string(raw.size()) + " bytes)");
    return raw;
}

void Pop3Client::quit() {
    try {
        if (conn_.isOpen()) command("QUIT");
    }
    catch (...) {
        // Closing anyway; an error while saying goodbye does not matter.
    }
    conn_.close();
}