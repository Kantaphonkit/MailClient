// MailService.cpp
#include "MailService.h"

#include <chrono>
#include <stdexcept>

#include "Logger.h"
#include "Pop3Client.h"
#include "ProtocolErrors.h"
#include "SmtpClient.h"

namespace {
    using Clock = std::chrono::steady_clock;

    long long millisSince(Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }
}  // namespace

MailService::MailService(const Config& cfg)
    : cfg_(cfg), mailbox_(cfg.mailboxDir, cfg.username) {
}

InboxResult MailService::checkInbox() {
    InboxResult result;
    auto start = Clock::now();

    Pop3Client pop(cfg_.server, cfg_.pop3Port, cfg_.timeoutSeconds * 1000);
    pop.setTrace(trace_);
    pop.connect();
    try {
        pop.login(cfg_.username, password_);
    }
    catch (const AuthError&) {
        password_.clear();  // forget the wrong password so the user is asked again
        throw;
    }

    std::vector<Pop3ListEntry> entries = pop.list();
    result.onServer = static_cast<int>(entries.size());
    for (const Pop3ListEntry& entry : entries) {
        if (mailbox_.save(pop.retr(entry.number))) ++result.newSaved;
    }
    pop.quit();

    result.millis = millisSince(start);
    Logger::info("Inbox checked: " + std::to_string(result.onServer) + " on server, " +
        std::to_string(result.newSaved) + " new, " + std::to_string(result.millis) + " ms");
    return result;
}

long long MailService::send(MailMessage msg) {
    msg.from = cfg_.email;
    if (msg.to.empty()) throw std::invalid_argument("At least one recipient is required");
    for (const std::string& rcpt : msg.to) {
        if (!MailMessage::isValidAddress(rcpt)) {
            throw std::invalid_argument("Invalid recipient address: '" + rcpt + "'");
        }
    }

    auto start = Clock::now();
    SmtpClient smtp(cfg_.server, cfg_.smtpPort, cfg_.timeoutSeconds * 1000);
    smtp.setTrace(trace_);
    smtp.send(msg);
    long long ms = millisSince(start);

    Logger::info("Send time: " + std::to_string(ms) + " ms (" + std::to_string(msg.body.size()) +
        " byte body)");
    return ms;
}