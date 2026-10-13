// Account.cpp
#include "Account.h"

#include <stdexcept>

#include "MailMessage.h"

Account Account::forEmail(const std::string& email, const Config& cfg) {
    if (!MailMessage::isValidAddress(email)) {
        throw std::invalid_argument("'" + email + "' is not a valid email address");
    }
    Account acc;
    acc.email = email;
    acc.server = cfg.server;
    acc.smtpPort = cfg.smtpPort;
    acc.pop3Port = cfg.pop3Port;
    return acc;
}