// Cli.h
// Command-line user interface: the menu, reading user input, printing results.
// It contains no protocol code; every real action goes through MailService.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "MailService.h"

class Cli {
public:
    explicit Cli(MailService& service) : service_(service) {}

    // Show the menu until the user chooses Exit (or input ends).
    void run();

private:
    // Menu actions
    void checkInbox();
    void readMessage();
    void compose();
    void viewLocal();

    // Helpers
    bool readLine(std::string& out);                       // false when input has ended
    std::string ask(const std::string& prompt);
    std::string askPassword(const std::string& prompt);    // input shown as ****
    bool ensurePassword();
    void printList(const std::vector<StoredMessage>& messages);
    void guarded(const std::function<void()>& action);     // run action, turn errors into messages

    MailService& service_;
    bool inputEnded_ = false;
};