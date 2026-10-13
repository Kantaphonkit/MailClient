// MailService.cpp
#include "MailService.h"

#include <chrono>
#include <stdexcept>

#include "Logger.h"
#include "Pop3Client.h"
#include "ProtocolErrors.h"
#include "SmtpClient.h"
#include "TcpConnection.h"

namespace {
    using Clock = std::chrono::steady_clock;

    long long millisSince(Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }
}  // namespace

void MailService::requireLogin() const {
    if (!loggedIn()) throw std::logic_error("No account is logged in");
}

void MailService::login(const Account& acc) {
    account_ = acc;
    mailbox_.emplace(cfg_.mailboxDir, acc.email);  // each account has its own local folder
    Logger::info("Account selected: " + acc.email + " (server " + acc.server + ")");
}

void MailService::logout() {
    if (loggedIn()) Logger::info("Logged out: " + account_.email);
    account_ = Account{};  // also forgets the password
    mailbox_.reset();
}

void MailService::verifyLogin() {
    requireLogin();
    try {
        verifyLoginImpl();
    }
    catch (const NetworkError& e) {
        Logger::error(std::string("POP3: ") + e.what());
        throw;
    }
}

void MailService::verifyLoginImpl() {
    Pop3Client pop(account_.server, account_.pop3Port, cfg_.timeoutSeconds * 1000);
    pop.setTrace(trace_);
    pop.connect();
    try {
        pop.login(account_.email, account_.password);
    }
    catch (const AuthError&) {
        account_.password.clear();
        throw;
    }
    pop.quit();
}

InboxResult MailService::checkInbox() {
    requireLogin();
    try {
        return checkInboxImpl();
    }
    catch (const NetworkError& e) {
        Logger::error(std::string("POP3: ") + e.what());  // e.g. server down, timeout
        throw;
    }
}

InboxResult MailService::checkInboxImpl() {
    InboxResult result;
    auto start = Clock::now();

    Pop3Client pop(account_.server, account_.pop3Port, cfg_.timeoutSeconds * 1000);
    pop.setTrace(trace_);
    pop.connect();
    try {
        pop.login(account_.email, account_.password);
    }
    catch (const AuthError&) {
        account_.password.clear();  // forget the wrong password so the user is asked again
        throw;
    }

    std::vector<Pop3ListEntry> entries = pop.list();
    result.onServer = static_cast<int>(entries.size());
    for (const Pop3ListEntry& entry : entries) {
        if (mailbox_->save(pop.retr(entry.number))) ++result.newSaved;
    }
    pop.quit();

    result.millis = millisSince(start);
    Logger::info("Inbox checked: " + std::to_string(result.onServer) + " on server, " +
        std::to_string(result.newSaved) + " new, " + std::to_string(result.millis) + " ms");
    return result;
}

long long MailService::send(MailMessage msg) {
    requireLogin();
    msg.from = account_.email;
    if (msg.to.empty()) throw std::invalid_argument("At least one recipient is required");
    for (const std::string& rcpt : msg.to) {
        if (!MailMessage::isValidAddress(rcpt)) {
            throw std::invalid_argument("Invalid recipient address: '" + rcpt + "'");
        }
    }

    auto start = Clock::now();
    SmtpClient smtp(account_.server, account_.smtpPort, cfg_.timeoutSeconds * 1000);
    smtp.setTrace(trace_);
    smtp.send(msg);
    long long ms = millisSince(start);

    Logger::info("Send time: " + std::to_string(ms) + " ms (" + std::to_string(msg.body.size()) +
        " byte body)");
    return ms;
}

std::vector<StoredMessage> MailService::localMessages() const {
    requireLogin();
    return mailbox_->loadAll();
}