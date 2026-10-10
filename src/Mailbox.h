// Mailbox.h
// Local copy of downloaded messages, so they can be read again after a restart
// without connecting to the server.
//
// Storage: one raw message per file, exactly as received from POP3:
//   data/mailbox/User_2@test.local/000001.eml
//   data/mailbox/User_2@test.local/000002.eml
// .eml is the standard file type for a single email; Outlook/Thunderbird can open it.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "MailMessage.h"

struct StoredMessage {
    std::string fileName;  // e.g. "000002.eml"
    MailMessage message;   // parsed fields for display
};

class Mailbox {
public:
    // Opens (and creates if needed) the folder <baseDir>/<account>.
    Mailbox(const std::string& baseDir, const std::string& account);

    // Save one raw message. Returns false if it is already stored (same Message-ID).
    bool save(const std::string& raw);

    // All stored messages, oldest first.
    std::vector<StoredMessage> loadAll() const;

    // The raw text of one stored file (for showing the full message).
    std::string loadRaw(const std::string& fileName) const;

    const std::string& folder() const { return dir_; }

private:
    static std::string keyFor(const MailMessage& msg, const std::string& raw);

    std::string dir_;
    std::set<std::string> knownKeys_;  // Message-IDs already on disk
    int nextNumber_ = 1;               // number for the next file name
};