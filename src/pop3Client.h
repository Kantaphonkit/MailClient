// Pop3Client.h
// Receives mail with the minimum POP3 command set required by the project:
//   USER / PASS -> STAT / LIST -> RETR n -> QUIT
// POP3 replies start with "+OK" (success) or "-ERR" (failure).
// LIST and RETR replies are multi-line and end with a line holding only ".".
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "TcpConnection.h"

struct Pop3ListEntry {
    int number = 0;       // message number on the server (1, 2, 3, ...)
    std::size_t size = 0; // size in bytes ("octets")
};

class Pop3Client {
public:
    Pop3Client(std::string host, int port, int timeoutMs = 10000);

    // Print "C: ..." / "S: ..." lines. The password is never printed.
    void setTrace(bool on) { trace_ = on; }

    void connect();                                         // connect + read "+OK" greeting
    void login(const std::string& user, const std::string& password);  // throws AuthError
    std::pair<int, std::size_t> stat();                     // (message count, total bytes)
    std::vector<Pop3ListEntry> list();                      // one entry per message
    std::string retr(int number);                           // full raw message (CRLF lines)
    void quit();                                            // QUIT + close

private:
    std::string readStatus();                       // one "+OK"/"-ERR" line; returns text after +OK
    std::string command(const std::string& line);   // send a line, then readStatus()
    std::vector<std::string> readMultiline();       // lines until ".", dot-stuffing removed

    TcpConnection conn_;
    std::string host_;
    int port_;
    int timeoutMs_;
    bool trace_ = false;
};