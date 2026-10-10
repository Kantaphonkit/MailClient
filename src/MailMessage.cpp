// MailMessage.cpp
#include "MailMessage.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace {

    // Remove CR and LF so a value can never start a new header line
    // (otherwise a subject like "Hi\r\nBcc: x@y" would inject an extra header).
    std::string singleLine(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c != '\r' && c != '\n') out += c;
        }
        return out;
    }

    // Turn any mix of "\n", "\r\n" or "\r" into CRLF, as email requires.
    std::string toCrlf(const std::string& text) {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '\r') {
                out += "\r\n";
                if (i + 1 < text.size() && text[i + 1] == '\n') ++i;  // skip LF of a CRLF pair
            }
            else if (c == '\n') {
                out += "\r\n";
            }
            else {
                out += c;
            }
        }
        return out;
    }

    std::string trim(const std::string& s) {
        size_t b = s.find_first_not_of(" \t");
        if (b == std::string::npos) return "";
        size_t e = s.find_last_not_of(" \t");
        return s.substr(b, e - b + 1);
    }

    std::string toLower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // "Alice <alice@x.com>" -> "alice@x.com";  "<a@x>" -> "a@x";  "a@x" -> "a@x"
    std::string extractAddress(const std::string& value) {
        size_t lt = value.find('<');
        size_t gt = value.find('>', lt == std::string::npos ? 0 : lt);
        if (lt != std::string::npos && gt != std::string::npos) {
            return trim(value.substr(lt + 1, gt - lt - 1));
        }
        return trim(value);
    }

}  // namespace

MailMessage MailMessage::parse(const std::string& raw) {
    MailMessage msg;

    // 1. Split into lines (accept CRLF or LF).
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find('\n', start);
        if (end == std::string::npos) end = raw.size();
        std::string line = raw.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }

    // 2. Headers end at the first empty line. A line starting with space/tab
    //    continues the previous header ("folding"), so join it back on.
    std::vector<std::string> headers;
    size_t i = 0;
    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) {
            ++i;  // skip the empty separator line
            break;
        }
        if ((line[0] == ' ' || line[0] == '\t') && !headers.empty()) {
            headers.back() += " " + trim(line);
        }
        else {
            headers.push_back(line);
        }
    }

    // 3. Pick out the headers we need. Header names are case-insensitive.
    for (const std::string& h : headers) {
        size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        std::string name = toLower(trim(h.substr(0, colon)));
        std::string value = trim(h.substr(colon + 1));

        if (name == "from") {
            msg.from = extractAddress(value);
        }
        else if (name == "to") {
            std::stringstream parts(value);
            std::string part;
            while (std::getline(parts, part, ',')) {
                std::string addr = extractAddress(part);
                if (!addr.empty()) msg.to.push_back(addr);
            }
        }
        else if (name == "subject") {
            msg.subject = value;
        }
        else if (name == "date") {
            msg.date = value;
        }
        else if (name == "message-id") {
            msg.messageId = value;
        }
    }

    // 4. Everything after the empty line is the body.
    for (; i < lines.size(); ++i) {
        msg.body += lines[i];
        if (i + 1 < lines.size()) msg.body += "\n";
    }
    return msg;
}

std::string MailMessage::toWireFormat() const {
    std::string toHeader;
    for (size_t i = 0; i < to.size(); ++i) {
        if (i > 0) toHeader += ", ";
        toHeader += "<" + to[i] + ">";
    }

    std::ostringstream out;
    out << "From: <" << from << ">\r\n"
        << "To: " << toHeader << "\r\n"
        << "Subject: " << singleLine(subject) << "\r\n"
        << "Date: " << (date.empty() ? currentDate() : date) << "\r\n"
        << "Message-ID: " << (messageId.empty() ? generateMessageId(from) : messageId) << "\r\n"
        << "MIME-Version: 1.0\r\n"
        << "Content-Type: text/plain; charset=UTF-8\r\n"
        << "Content-Transfer-Encoding: 8bit\r\n"
        << "\r\n"  // empty line: headers end, body starts
        << toCrlf(body);

    std::string result = out.str();
    if (result.size() < 2 || result.compare(result.size() - 2, 2, "\r\n") != 0) {
        result += "\r\n";  // the message must end with a line break before the final "."
    }
    return result;
}

bool MailMessage::isValidAddress(const std::string& address) {
    size_t at = address.find('@');
    if (at == std::string::npos || at == 0 || at == address.size() - 1) return false;
    if (address.find('@', at + 1) != std::string::npos) return false;  // only one '@'
    if (address.find('.', at) == std::string::npos) return false;      // domain needs a dot
    for (char c : address) {
        if (c == ' ' || c == '<' || c == '>' || c == '\r' || c == '\n' || c == ',') return false;
    }
    return true;
}

std::string MailMessage::currentDate() {
    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%a, %d %b %Y %H:%M:%S %z");  // %z = offset like +0800
    return out.str();
}

std::string MailMessage::generateMessageId(const std::string& from) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(1000, 9999);

    size_t at = from.find('@');
    std::string domain = (at == std::string::npos) ? "localhost" : from.substr(at + 1);
    return "<" + std::to_string(ms) + "." + std::to_string(dist(rng)) + "@" + domain + ">";
}