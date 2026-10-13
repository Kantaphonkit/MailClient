// MailBench.cpp — performance test for the mail client.
// Uses the SAME code as the real client (MailService, SmtpClient, Pop3Client from mailcore),
// so the numbers measure our own SMTP/POP3 implementation.
//
//   Test A  POP3 login only        connect + USER/PASS + QUIT  (connection overhead)
//   Test B  Send (SMTP)            1 KB, 10 KB, 100 KB, 1 MB bodies
//   Test C  Retrieve (POP3 RETR)   each message sent in Test B, downloaded again
//   Test D  Check Inbox            the whole action behind the "Check Inbox" button
//
// Usage:  MailBench [runs]      (default 5 runs per measurement)
// Results are printed and saved to perf_results.md and perf_results.csv.
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Account.h"
#include "Config.h"
#include "Logger.h"
#include "MailMessage.h"
#include "MailService.h"
#include "Pop3Client.h"
#include "ProtocolErrors.h"
#include "TcpConnection.h"

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

    // ------------------------------------------------------------------ small helpers

    double msSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    struct Stats {
        std::vector<double> ms;
        double avg() const {
            double sum = 0;
            for (double v : ms) sum += v;
            return ms.empty() ? 0 : sum / ms.size();
        }
        double min() const { return ms.empty() ? 0 : *std::min_element(ms.begin(), ms.end()); }
        double max() const { return ms.empty() ? 0 : *std::max_element(ms.begin(), ms.end()); }
    };

    std::string fixed(double v, int digits = 1) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(digits) << v;
        return out.str();
    }

    std::string sizeLabel(std::size_t bytes) {
        if (bytes >= 1024 * 1024) return fixed(bytes / (1024.0 * 1024.0), bytes % (1024 * 1024) ? 2 : 0) + " MB";
        if (bytes >= 1024) return fixed(bytes / 1024.0, bytes % 1024 ? 1 : 0) + " KB";
        return std::to_string(bytes) + " B";
    }

    // Throughput in MB/s (1 MB = 1,000,000 bytes, the usual unit for network speed).
    std::string throughput(std::size_t bytes, double ms) {
        if (ms <= 0) return "-";
        return fixed(bytes / (ms / 1000.0) / 1e6, 2) + " MB/s";
    }

    std::string ask(const std::string& prompt, const std::string& fallback = "") {
        std::cout << prompt;
        if (!fallback.empty()) std::cout << " [" << fallback << "]";
        std::cout << ": " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        return line.empty() ? fallback : line;
    }

    // Password input shown as **** (same idea as the CLI). Never printed or logged.
    std::string askPassword(const std::string& prompt) {
        std::cout << prompt << ": " << std::flush;
        std::string pw;
#ifdef _WIN32
        while (true) {
            int ch = _getch();
            if (ch == '\r' || ch == '\n') break;
            if (ch == '\b') {
                if (!pw.empty()) { pw.pop_back(); std::cout << "\b \b" << std::flush; }
            }
            else if (ch == 0 || ch == 224) {
                _getch();
            }
            else {
                pw += static_cast<char>(ch);
                std::cout << '*' << std::flush;
            }
        }
#else
        bool tty = isatty(STDIN_FILENO);
        termios oldMode{};
        if (tty) {
            tcgetattr(STDIN_FILENO, &oldMode);
            termios noEcho = oldMode;
            noEcho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
        }
        std::getline(std::cin, pw);
        if (tty) tcsetattr(STDIN_FILENO, TCSANOW, &oldMode);
#endif
        std::cout << "\n";
        return pw;
    }

    // A plain-text body of exactly `bytes` bytes, in readable 72-character lines.
    std::string makeBody(std::size_t bytes) {
        std::string body;
        body.reserve(bytes + 100);
        int n = 0;
        while (body.size() < bytes) {
            std::ostringstream line;
            line << "Benchmark line " << std::setw(6) << std::setfill('0') << ++n
                << ": the quick brown fox jumps over the lazy dog.\n";
            body += line.str();
        }
        body.resize(bytes);
        return body;
    }

    std::string timestampId() {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y%m%d-%H%M%S");
        return out.str();
    }

    Account makeAccount(const std::string& email, const std::string& password, const Config& cfg) {
        Account acc = Account::forEmail(email, cfg);
        acc.password = password;
        return acc;
    }

}  // namespace

int main(int argc, char** argv) {
    int runs = 5;
    if (argc > 1) runs = std::max(1, std::atoi(argv[1]));
    const std::vector<std::size_t> sizes = { 1024, 10 * 1024, 100 * 1024, 1024 * 1024 };

    try {
        Config cfg = Config::load("config/client.conf");
        Logger::init("logs/bench.log");
        SocketSystem sockets;

        std::cout << "=== MailBench: performance test (" << runs << " runs per measurement) ===\n"
            << "Server: " << cfg.server << "  SMTP " << cfg.smtpPort << "  POP3 " << cfg.pop3Port
            << "\n\n";
        std::string senderEmail = ask("Sender email", cfg.defaultEmail);
        std::string senderPw = askPassword("Sender password");
        std::string receiverEmail = ask("Receiver email (a test account is best)", "User_1@test.local");
        std::string receiverPw = askPassword("Receiver password");

        Account sender = makeAccount(senderEmail, senderPw, cfg);
        Account receiver = makeAccount(receiverEmail, receiverPw, cfg);

        MailService senderSvc(cfg);
        senderSvc.login(sender);
        senderSvc.verifyLogin();
        MailService receiverSvc(cfg);
        receiverSvc.login(receiver);
        receiverSvc.verifyLogin();
        std::cout << "\nBoth logins OK.\n";

        const std::string runId = timestampId();
        const std::string subjectPrefix = "[bench " + runId + "] ";

        // Warm-up (not measured): the first connection has one-time costs.
        {
            MailMessage warm;
            warm.to = { receiverEmail };
            warm.subject = subjectPrefix + "warm-up";
            warm.body = "warm-up";
            senderSvc.send(warm);
            receiverSvc.verifyLogin();
        }

        // ---------------------------------------------------------- Test A
        std::cout << "\n[A] POP3 login only..." << std::flush;
        Stats loginStats;
        for (int i = 0; i < runs; ++i) {
            auto t = Clock::now();
            receiverSvc.verifyLogin();
            loginStats.ms.push_back(msSince(t));
        }
        std::cout << " done\n";

        // ---------------------------------------------------------- Test B
        std::map<std::size_t, Stats> sendStats;
        std::map<std::size_t, std::size_t> sendWire;  // bytes on the wire for each size
        for (std::size_t size : sizes) {
            std::cout << "[B] Sending " << sizeLabel(size) << " x" << runs << "..." << std::flush;
            for (int i = 1; i <= runs; ++i) {
                MailMessage msg;
                msg.from = senderEmail;
                msg.to = { receiverEmail };
                msg.subject = subjectPrefix + sizeLabel(size) + " #" + std::to_string(i);
                msg.body = makeBody(size);
                sendWire[size] = msg.toWireFormat().size();
                auto t = Clock::now();
                senderSvc.send(msg);
                sendStats[size].ms.push_back(msSince(t));
            }
            std::cout << " done\n";
        }

        // ---------------------------------------------------------- Test C
        // Download each message from Test B again with RETR and time each RETR on its own.
        std::map<std::size_t, Stats> retrStats;
        std::map<std::size_t, std::size_t> retrBytes;
        int serverCount = 0;
        std::size_t serverBytes = 0;
        for (int round = 1; round <= runs; ++round) {
            std::cout << "[C] Retrieve round " << round << "/" << runs << "..." << std::flush;
            Pop3Client pop(receiver.server, receiver.pop3Port, cfg.timeoutSeconds * 1000);
            pop.connect();
            pop.login(receiver.email, receiver.password);
            auto st = pop.stat();
            serverCount = st.first;
            serverBytes = st.second;
            for (const Pop3ListEntry& entry : pop.list()) {
                auto t = Clock::now();
                std::string raw = pop.retr(entry.number);
                double ms = msSince(t);
                std::string subject = MailMessage::parse(raw).subject;
                if (subject.rfind(subjectPrefix, 0) != 0) continue;  // not from this benchmark run
                for (std::size_t size : sizes) {
                    if (subject.find("] " + sizeLabel(size) + " #") != std::string::npos) {
                        retrStats[size].ms.push_back(ms);
                        retrBytes[size] = raw.size();
                    }
                }
            }
            pop.quit();
            std::cout << " done\n";
        }

        // ---------------------------------------------------------- Test D
        // The full "Check Inbox" action: POP3 session + parse + save to the local mailbox.
        // Uses a separate bench_data folder so the real data/mailbox is not touched.
        Stats inboxNew, inboxNone;
        int inboxCount = 0;
        std::cout << "[D] Check Inbox (all new / nothing new)..." << std::flush;
        for (int i = 0; i < runs; ++i) {
            fs::remove_all("bench_data/fresh");
            Config fresh = cfg;
            fresh.mailboxDir = "bench_data/fresh";
            MailService svc(fresh);
            svc.login(receiver);
            auto t = Clock::now();
            InboxResult r = svc.checkInbox();
            inboxNew.ms.push_back(msSince(t));
            inboxCount = r.onServer;
        }
        {
            Config keep = cfg;
            keep.mailboxDir = "bench_data/keep";
            MailService svc(keep);
            svc.login(receiver);
            svc.checkInbox();  // first time fills the folder (not measured)
            for (int i = 0; i < runs; ++i) {
                auto t = Clock::now();
                svc.checkInbox();
                inboxNone.ms.push_back(msSince(t));
            }
        }
        fs::remove_all("bench_data");
        std::cout << " done\n";

        // ---------------------------------------------------------- report
#ifdef NDEBUG
        const char* build = "Release";
#else
        const char* build = "Debug";
#endif
        std::ostringstream md;
        md << "# MailBench results\n\n"
            << "- Run: " << runId << ", " << runs << " runs per measurement, " << build << " build\n"
            << "- Server: " << cfg.server << " (SMTP " << cfg.smtpPort << ", POP3 " << cfg.pop3Port << ")\n"
            << "- Sender: " << senderEmail << ", receiver: " << receiverEmail << "\n"
            << "- Times in milliseconds (ms); a warm-up send and login were done first and not counted.\n\n";

        md << "## A. POP3 login only (connect + USER/PASS + QUIT)\n\n"
            << "| Runs | Avg (ms) | Min (ms) | Max (ms) |\n|---|---|---|---|\n"
            << "| " << runs << " | " << fixed(loginStats.avg()) << " | " << fixed(loginStats.min()) << " | "
            << fixed(loginStats.max()) << " |\n\n";

        md << "## B. Send (SMTP: EHLO, MAIL FROM, RCPT TO, DATA, QUIT)\n\n"
            << "| Body size | Bytes sent | Runs | Avg (ms) | Min (ms) | Max (ms) | Throughput |\n"
            << "|---|---|---|---|---|---|---|\n";
        for (std::size_t size : sizes) {
            const Stats& s = sendStats[size];
            md << "| " << sizeLabel(size) << " | " << sendWire[size] << " | " << s.ms.size() << " | "
                << fixed(s.avg()) << " | " << fixed(s.min()) << " | " << fixed(s.max()) << " | "
                << throughput(sendWire[size], s.avg()) << " |\n";
        }

        md << "\n## C. Retrieve one message (POP3 RETR)\n\n"
            << "| Body size | Bytes received | Samples | Avg (ms) | Min (ms) | Max (ms) | Throughput |\n"
            << "|---|---|---|---|---|---|---|\n";
        for (std::size_t size : sizes) {
            const Stats& s = retrStats[size];
            md << "| " << sizeLabel(size) << " | " << retrBytes[size] << " | " << s.ms.size() << " | "
                << fixed(s.avg()) << " | " << fixed(s.min()) << " | " << fixed(s.max()) << " | "
                << throughput(retrBytes[size], s.avg()) << " |\n";
        }

        md << "\n## D. Check Inbox (whole action: login, LIST, RETR all, parse, save, QUIT)\n\n"
            << "Mailbox on server: " << serverCount << " messages, " << sizeLabel(serverBytes) << "\n\n"
            << "| Case | Runs | Avg (ms) | Min (ms) | Max (ms) |\n|---|---|---|---|---|\n"
            << "| All " << inboxCount << " messages new (saved to disk) | " << runs << " | "
            << fixed(inboxNew.avg()) << " | " << fixed(inboxNew.min()) << " | " << fixed(inboxNew.max()) << " |\n"
            << "| Nothing new (already saved) | " << runs << " | " << fixed(inboxNone.avg()) << " | "
            << fixed(inboxNone.min()) << " | " << fixed(inboxNone.max()) << " |\n";

        std::cout << "\n" << md.str() << "\n";
        std::ofstream("perf_results.md") << md.str();

        // CSV: one row per measurement, for Excel / charts in the report
        std::ofstream csv("perf_results.csv");
        csv << "test,size_bytes,wire_bytes,runs,avg_ms,min_ms,max_ms\n";
        csv << "pop3_login,0,0," << runs << "," << fixed(loginStats.avg(), 2) << ","
            << fixed(loginStats.min(), 2) << "," << fixed(loginStats.max(), 2) << "\n";
        for (std::size_t size : sizes) {
            const Stats& s = sendStats[size];
            csv << "send," << size << "," << sendWire[size] << "," << s.ms.size() << "," << fixed(s.avg(), 2)
                << "," << fixed(s.min(), 2) << "," << fixed(s.max(), 2) << "\n";
        }
        for (std::size_t size : sizes) {
            const Stats& s = retrStats[size];
            csv << "retrieve," << size << "," << retrBytes[size] << "," << s.ms.size() << ","
                << fixed(s.avg(), 2) << "," << fixed(s.min(), 2) << "," << fixed(s.max(), 2) << "\n";
        }
        csv << "check_inbox_all_new," << serverBytes << ",," << runs << "," << fixed(inboxNew.avg(), 2) << ","
            << fixed(inboxNew.min(), 2) << "," << fixed(inboxNew.max(), 2) << "\n";
        csv << "check_inbox_none_new," << serverBytes << ",," << runs << "," << fixed(inboxNone.avg(), 2) << ","
            << fixed(inboxNone.min(), 2) << "," << fixed(inboxNone.max(), 2) << "\n";

        std::cout << "Saved: " << fs::absolute("perf_results.md").string() << "\n"
            << "       " << fs::absolute("perf_results.csv").string() << "\n";
        return 0;
    }
    catch (const AuthError& e) {
        std::cerr << "\n[!] Login failed: " << e.what() << "\n";
    }
    catch (const NetworkError& e) {
        std::cerr << "\n[!] Cannot reach the mail server: " << e.what() << "\n";
    }
    catch (const std::exception& e) {
        std::cerr << "\n[!] " << e.what() << "\n";
    }
    return 1;
}