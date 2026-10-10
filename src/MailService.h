// MailService.h
// The actions of the mail client, independent of the user interface.
// The CLI (Cli.cpp) calls these functions; a GUI could call the very same ones.
//
//   checkInbox()     POP3: download all messages, save new ones locally
//   send()           SMTP: send one message
//   localMessages()  read the local mailbox (works offline)
#pragma once

#include <string>
#include <vector>

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
    explicit MailService(const Config& cfg);

    const Config& config() const { return cfg_; }

    // The password is kept in memory only for this run; it is never written anywhere.
    void setPassword(const std::string& password) { password_ = password; }
    bool hasPassword() const { return !password_.empty(); }
    void clearPassword() { password_.clear(); }

    // Print the SMTP/POP3 conversation ("C: ..." / "S: ...") on the console.
    void setTrace(bool on) { trace_ = on; }
    bool trace() const { return trace_; }

    // Throws NetworkError, AuthError (wrong password) or ProtocolError.
    InboxResult checkInbox();

    // Fills in the sender from the config. Returns the time taken in milliseconds.
    // Throws std::invalid_argument for bad input, NetworkError or ProtocolError.
    long long send(MailMessage msg);

    std::vector<StoredMessage> localMessages() const { return mailbox_.loadAll(); }

private:
    Config cfg_;
    Mailbox mailbox_;
    std::string password_;
    bool trace_ = false;
};