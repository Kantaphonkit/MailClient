// SmtpClient.h
// Sends one email with the minimum SMTP command set required by the project:
//   EHLO (falls back to HELO) -> MAIL FROM -> RCPT TO (once per recipient) -> DATA -> QUIT
// Every server reply is read with readReply() and its code is checked.
#pragma once

#include <string>

#include "MailMessage.h"
#include "TcpConnection.h"

// One SMTP reply. A reply can span several lines ("250-..." ... "250 ..."),
// but it always has one 3-digit code.
struct SmtpReply {
    int code = 0;
    std::string text;  // all lines' text joined with '\n'
};

class SmtpClient {
public:
    SmtpClient(std::string host, int port, int timeoutMs = 10000);

    // Print every command and reply as "C: ..." / "S: ..." (handy for demos and the report).
    void setTrace(bool on) { trace_ = on; }

    // Run one complete SMTP session that delivers msg.
    // Throws NetworkError (connection problem) or ProtocolError (server said no).
    void send(const MailMessage& msg);

private:
    SmtpReply readReply();
    SmtpReply command(const std::string& line);              // send a line, read the reply
    void expect(const SmtpReply& reply, int code, const std::string& step);
    void sendBody(const std::string& wireText);              // dot-stuffing + final "."
    void quietQuit();                                        // best-effort QUIT after an error

    TcpConnection conn_;
    std::string host_;
    int port_;
    int timeoutMs_;
    bool trace_ = false;
};