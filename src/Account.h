// Account.h
// Everything needed to use one mail account. Filled in at the login screen.
// useTls and smtpAuth are already here for a later bonus (QQ / 163 / Gmail need both);
// for the local hMailServer both stay false.
#pragma once

#include <string>

#include "Config.h"

struct Account {
    std::string email;     // also the POP3 login name (hMailServer uses the full address)
    std::string password;  // kept in memory only, never written to disk or log
    std::string server;
    int smtpPort = 25;
    int pop3Port = 110;
    bool useTls = false;    // encrypted connection (not implemented yet)
    bool smtpAuth = false;  // SMTP login before sending (not implemented yet)

    // Server settings for an address. For now every address uses the server from the
    // config file; known providers (qq.com, 163.com, gmail.com) can be added here later.
    static Account forEmail(const std::string& email, const Config& cfg);
};