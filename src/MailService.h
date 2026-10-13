// MailService.h
// The actions of the mail client, independent of the user interface.
// The CLI (Cli.cpp) calls these functions; a GUI could call the very same ones.
//
//   login() / verifyLogin()  choose the account and check its password
//   checkInbox()             POP3: download all messages, save new ones locally
//   send()                   SMTP: send one message
//   localMessages()          read the local mailbox of the current account (works offline)
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Account.h"
#include "Config.h"
#include "Mailbox.h"
#include "MailMessage.h"

struct InboxResult {
    int onServer = 0;     // messages currently on the server
    int newSaved = 0;     // of those, newly saved to the local mailbox
    long long millis = 0; // time for the whole POP3 session
};

class MailService {
public:
    explicit MailService(const Config& cfg) : cfg_(cfg) {}

    const Config& config() const { return cfg_; }

    // Make acc the current account and open its local mailbox. No network yet.
    void login(const Account& acc);
    // Check the password with a short POP3 session (USER / PASS / QUIT).
    // Throws AuthError (wrong password) or NetworkError (server unreachable).
    void verifyLogin();
    void logout();

    bool loggedIn() const { return mailbox_.has_value(); }
    const Account& account() const { return account_; }

    // The password is kept in memory only for this run; it is never written anywhere.
    void setPassword(const std::string& password) { account_.password = password; }
    bool hasPassword() const { return !account_.password.empty(); }

    // Print the SMTP/POP3 conversation ("C: ..." / "S: ...") on the console.
    void setTrace(bool on) { trace_ = on; }
    bool trace() const { return trace_; }

    // Throws NetworkError, AuthError (wrong password) or ProtocolError.
    InboxResult checkInbox();

    // Sender = current account. Returns the time taken in milliseconds.
    // Throws std::invalid_argument for bad input, NetworkError or ProtocolError.
    long long send(MailMessage msg);

    std::vector<StoredMessage> localMessages() const;

private:
    void requireLogin() const;
    void verifyLoginImpl();
    InboxResult checkInboxImpl();

    Config cfg_;
    Account account_;
    std::optional<Mailbox> mailbox_;  // empty until someone logs in
    bool trace_ = false;
};