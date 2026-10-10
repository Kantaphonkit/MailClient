// Cli.cpp
#include "Cli.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "Logger.h"
#include "ProtocolErrors.h"
#include "TcpConnection.h"

#ifdef _WIN32
#include <conio.h>  // _getch(): read one key without showing it
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

    std::string fit(const std::string& s, size_t width) {
        return s.size() <= width ? s : s.substr(0, width - 3) + "...";
    }

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    }

    // Parse a whole-number choice; returns -1 if the text is not a plain number.
    int toNumber(const std::string& text) {
        std::string t = trim(text);
        if (t.empty() || t.size() > 6) return -1;
        for (char c : t) {
            if (c < '0' || c > '9') return -1;
        }
        return std::stoi(t);
    }

}  // namespace

// ------------------------------------------------------------------ input helpers

bool Cli::readLine(std::string& out) {
    if (!std::getline(std::cin, out)) {
        inputEnded_ = true;  // Ctrl+Z / end of input: leave the menu cleanly
        return false;
    }
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}

std::string Cli::ask(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    readLine(line);
    return line;
}

std::string Cli::askPassword(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string password;
#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') break;          // Enter
        if (ch == 3) { inputEnded_ = true; break; }   // Ctrl+C
        if (ch == '\b') {                              // Backspace
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b" << std::flush;    // erase one '*' on screen
            }
        }
        else if (ch == 0 || ch == 224) {
            _getch();                                  // arrow/function key: ignore both bytes
        }
        else {
            password += static_cast<char>(ch);
            std::cout << '*' << std::flush;
        }
    }
    std::cout << "\n";
#else
    // Linux/macOS: switch off terminal echo while typing (if input is a terminal).
    bool tty = isatty(STDIN_FILENO);
    termios oldMode{};
    if (tty) {
        tcgetattr(STDIN_FILENO, &oldMode);
        termios noEcho = oldMode;
        noEcho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
    }
    readLine(password);
    if (tty) tcsetattr(STDIN_FILENO, TCSANOW, &oldMode);
    std::cout << "\n";
#endif
    return password;
}

bool Cli::ensurePassword() {
    if (service_.hasPassword()) return true;
    std::string pw = askPassword("Password for " + service_.config().username + ": ");
    if (pw.empty()) {
        std::cout << "No password entered.\n";
        return false;
    }
    service_.setPassword(pw);
    return true;
}

// Run one menu action. Every error becomes a clear message; the program never crashes.
void Cli::guarded(const std::function<void()>& action) {
    try {
        action();
    }
    catch (const NetworkError& e) {
        std::cout << "\n[!] Cannot reach the mail server: " << e.what() << "\n";
    }
    catch (const AuthError& e) {
        std::cout << "\n[!] " << e.what() << "\n    You will be asked for the password again.\n";
    }
    catch (const ProtocolError& e) {
        std::cout << "\n[!] The server refused the request: " << e.what() << "\n";
    }
    catch (const std::invalid_argument& e) {
        std::cout << "\n[!] Invalid input: " << e.what() << "\n";
    }
    catch (const std::exception& e) {
        Logger::error(std::string("Unexpected error: ") + e.what());
        std::cout << "\n[!] Error: " << e.what() << "\n";
    }
}

// ------------------------------------------------------------------ menu

void Cli::run() {
    const Config& cfg = service_.config();
    std::cout << "=== Lightweight Mail Client ===\n"
        << "Account: " << cfg.username << "   Server: " << cfg.server
        << " (SMTP " << cfg.smtpPort << ", POP3 " << cfg.pop3Port << ")\n";

    while (!inputEnded_) {
        std::cout << "\n1. Check Inbox\n"
            << "2. Read Message\n"
            << "3. Compose and Send Email\n"
            << "4. View Local Messages\n"
            << "5. Exit\n"
            << "6. Protocol trace: " << (service_.trace() ? "ON" : "OFF") << " (toggle)\n"
            << "> ";
        std::string choice;
        if (!readLine(choice)) break;

        switch (toNumber(choice)) {
        case 1: guarded([this] { checkInbox(); }); break;
        case 2: guarded([this] { readMessage(); }); break;
        case 3: guarded([this] { compose(); }); break;
        case 4: guarded([this] { viewLocal(); }); break;
        case 5: std::cout << "Bye.\n"; return;
        case 6: service_.setTrace(!service_.trace()); break;
        default: std::cout << "Please enter a number from 1 to 6.\n"; break;
        }
    }
}

void Cli::checkInbox() {
    if (!ensurePassword()) return;
    std::cout << "Connecting to POP3 server...\n";
    InboxResult r = service_.checkInbox();
    std::cout << "Server has " << r.onServer << " message(s), " << r.newSaved
        << " new. (" << r.millis << " ms)\n";
    printList(service_.localMessages());
}

void Cli::viewLocal() {
    printList(service_.localMessages());
}

void Cli::printList(const std::vector<StoredMessage>& messages) {
    std::cout << "\n" << std::left << std::setw(4) << "#" << std::setw(24) << "From"
        << std::setw(32) << "Subject" << "Date\n"
        << std::string(92, '-') << "\n";
    if (messages.empty()) std::cout << "(no messages - choose 1 to check the inbox)\n";
    for (size_t i = 0; i < messages.size(); ++i) {
        const MailMessage& m = messages[i].message;
        std::cout << std::setw(4) << i + 1
            << std::setw(24) << fit(m.from.empty() ? "(unknown)" : m.from, 23)
            << std::setw(32) << fit(m.subject.empty() ? "(no subject)" : m.subject, 31)
            << m.date << "\n";
    }
}

void Cli::readMessage() {
    std::vector<StoredMessage> messages = service_.localMessages();
    if (messages.empty()) {
        std::cout << "No local messages yet. Choose 1 to check the inbox first.\n";
        return;
    }
    printList(messages);
    std::string input = ask("\nMessage number (1-" + std::to_string(messages.size()) + "): ");
    int n = toNumber(input);
    if (n < 1 || n > static_cast<int>(messages.size())) {
        throw std::invalid_argument("'" + trim(input) + "' is not a message number from 1 to " +
            std::to_string(messages.size()));
    }

    const MailMessage& m = messages[n - 1].message;
    std::string to;
    for (size_t i = 0; i < m.to.size(); ++i) to += (i ? ", " : "") + m.to[i];
    std::cout << "\n" << std::string(60, '=') << "\n"
        << "From:    " << (m.from.empty() ? "(unknown)" : m.from) << "\n"
        << "To:      " << to << "\n"
        << "Subject: " << m.subject << "\n"
        << "Date:    " << m.date << "\n"
        << std::string(60, '-') << "\n"
        << m.body << "\n"
        << std::string(60, '=') << "\n"
        << "(stored as " << messages[n - 1].fileName << ")\n";
}

void Cli::compose() {
    MailMessage msg;

    std::string toLine = ask("To (separate several addresses with commas): ");
    std::stringstream parts(toLine);
    std::string part;
    while (std::getline(parts, part, ',')) {
        std::string addr = trim(part);
        if (addr.empty()) continue;
        if (!MailMessage::isValidAddress(addr)) {
            throw std::invalid_argument("'" + addr + "' is not a valid email address");
        }
        msg.to.push_back(addr);
    }
    if (msg.to.empty()) throw std::invalid_argument("No recipient entered");

    msg.subject = ask("Subject: ");

    std::cout << "Body (finish with a line containing only a single dot \".\"):\n";
    std::string line;
    bool first = true;
    while (readLine(line)) {
        if (line == ".") break;
        if (!first) msg.body += "\n";
        msg.body += line;
        first = false;
    }
    if (inputEnded_) return;

    std::string confirm = ask("Send to " + std::to_string(msg.to.size()) + " recipient(s)? (y/n): ");
    if (trim(confirm) != "y" && trim(confirm) != "Y") {
        std::cout << "Not sent.\n";
        return;
    }

    std::cout << "Connecting to SMTP server...\n";
    long long ms = service_.send(msg);
    std::cout << "Message sent successfully. (" << ms << " ms)\n";
}