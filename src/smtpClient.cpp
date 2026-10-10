// SmtpClient.cpp
#include "SmtpClient.h"

#include <cctype>
#include <iostream>
#include <utility>

#include "Logger.h"
#include "ProtocolErrors.h"

SmtpClient::SmtpClient(std::string host, int port, int timeoutMs)
    : host_(std::move(host)), port_(port), timeoutMs_(timeoutMs) {
}

// Read one (possibly multi-line) reply:
//   "250-first line"   '-' after the code = more lines follow
//   "250 last line"    ' ' after the code = this is the last line
SmtpReply SmtpClient::readReply() {
    SmtpReply reply;
    while (true) {
        std::string line = conn_.readLine();
        if (trace_) std::cout << "S: " << line << "\n";

        if (line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[0])) ||
            !std::isdigit(static_cast<unsigned char>(line[1])) ||
            !std::isdigit(static_cast<unsigned char>(line[2]))) {
            throw ProtocolError("Malformed SMTP reply: '" + line + "'");
        }

        reply.code = std::stoi(line.substr(0, 3));
        if (!reply.text.empty()) reply.text += "\n";
        reply.text += line.size() > 4 ? line.substr(4) : "";

        bool more = line.size() > 3 && line[3] == '-';
        if (!more) return reply;
    }
}

SmtpReply SmtpClient::command(const std::string& line) {
    if (trace_) std::cout << "C: " << line << "\n";
    conn_.sendLine(line);
    return readReply();
}

void SmtpClient::expect(const SmtpReply& reply, int code, const std::string& step) {
    if (reply.code != code) {
        throw ProtocolError(step + " failed: server replied " + std::to_string(reply.code) +
            " " + reply.text);
    }
}

// After DATA, the message is sent line by line and ends with a line holding only ".".
// So a body line that itself starts with "." must be sent as ".." (dot-stuffing, RFC 5321 4.5.2);
// the receiving server removes the extra dot again.
void SmtpClient::sendBody(const std::string& wireText) {
    std::string out;
    out.reserve(wireText.size() + 64);
    size_t start = 0;
    while (start < wireText.size()) {
        size_t end = wireText.find("\r\n", start);
        if (end == std::string::npos) end = wireText.size();
        std::string line = wireText.substr(start, end - start);
        if (!line.empty() && line[0] == '.') out += '.';
        out += line + "\r\n";
        start = end + 2;
    }
    out += ".\r\n";  // end-of-data marker
    if (trace_) std::cout << "C: [message: " << wireText.size() << " bytes]\nC: .\n";
    conn_.sendAll(out);  // one sendAll: TcpConnection loops until every byte is sent
}

void SmtpClient::quietQuit() {
    try {
        if (conn_.isOpen()) conn_.sendLine("QUIT");
    }
    catch (...) {
        // The session already failed; ignore errors while saying goodbye.
    }
    conn_.close();
}

void SmtpClient::send(const MailMessage& msg) {
    if (msg.to.empty()) throw ProtocolError("No recipients");

    conn_.setReadTimeout(timeoutMs_);
    conn_.connect(host_, port_, 5000);
    Logger::info("Connected to SMTP server " + host_ + ":" + std::to_string(port_));

    try {
        expect(readReply(), 220, "Greeting");

        // EHLO is the modern greeting; very old servers only know HELO.
        SmtpReply hello = command("EHLO student-client");
        if (hello.code >= 500) hello = command("HELO student-client");
        expect(hello, 250, "EHLO/HELO");

        expect(command("MAIL FROM:<" + msg.from + ">"), 250, "MAIL FROM");

        for (const std::string& rcpt : msg.to) {
            SmtpReply r = command("RCPT TO:<" + rcpt + ">");
            if (r.code != 250 && r.code != 251) {  // 251 = "user not local; will forward"
                throw ProtocolError("Recipient <" + rcpt + "> rejected: " +
                    std::to_string(r.code) + " " + r.text);
            }
        }

        expect(command("DATA"), 354, "DATA");
        sendBody(msg.toWireFormat());
        expect(readReply(), 250, "Message transfer");
        Logger::info("Message sent successfully to " + std::to_string(msg.to.size()) +
            " recipient(s)");

        SmtpReply bye = command("QUIT");
        if (bye.code != 221) Logger::error("Unexpected QUIT reply: " + std::to_string(bye.code));
        conn_.close();
    }
    catch (const ProtocolError& e) {
        Logger::error(std::string("SMTP: ") + e.what());
        quietQuit();
        throw;  // let the caller (menu) show the error
    }
    catch (const NetworkError& e) {
        Logger::error(std::string("SMTP: ") + e.what());
        conn_.close();
        throw;
    }
}