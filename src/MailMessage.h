// MailMessage.h
// One email: the fields the user types in, plus toWireFormat() which turns
// them into the text that is sent after the SMTP DATA command (RFC 5322).
#pragma once

#include <string>
#include <vector>

struct MailMessage {
    std::string from;
    std::vector<std::string> to;  // one or more recipients
    std::string subject;
    std::string date;       // filled in automatically if empty
    std::string messageId;  // filled in automatically if empty
    std::string body;

    // Headers + empty line + body, every line ending in CRLF.
    std::string toWireFormat() const;

    // The opposite of toWireFormat(): split a received raw message into its fields.
    static MailMessage parse(const std::string& raw);

    // Very small address check: "name@domain", no spaces, no <, >, CR or LF.
    static bool isValidAddress(const std::string& address);

    // Current local time in email format, e.g. "Thu, 08 Oct 2026 14:50:00 +0800".
    static std::string currentDate();

    // A unique id such as "<1791701400123.4821@test.local>".
    static std::string generateMessageId(const std::string& from);
};



