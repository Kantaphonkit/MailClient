// Mailbox.cpp
#include "Mailbox.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

    // Keep only characters that are safe in a folder name on Windows and Linux.
    std::string safeFolderName(const std::string& s) {
        std::string out;
        for (char c : s) {
            bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '@' || c == '.' || c == '_' ||
                c == '-';
            out += ok ? c : '_';
        }
        return out.empty() ? "default" : out;
    }

    std::string readFile(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);  // binary: keep CRLF exactly as stored
        if (!in) throw std::runtime_error("Cannot read " + path.string());
        std::ostringstream content;
        content << in.rdbuf();
        return content.str();
    }

}  // namespace

Mailbox::Mailbox(const std::string& baseDir, const std::string& account)
    : dir_((fs::path(baseDir) / safeFolderName(account)).string()) {
    fs::create_directories(dir_);

    // Scan what is already stored: remember each Message-ID and the highest file number.
    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (entry.path().extension() != ".eml") continue;
        std::string raw = readFile(entry.path());
        knownKeys_.insert(keyFor(MailMessage::parse(raw), raw));
        try {
            int n = std::stoi(entry.path().stem().string());
            nextNumber_ = std::max(nextNumber_, n + 1);
        }
        catch (...) {
            // a file not named like 000001.eml: ignore its number
        }
    }
}

// Message-ID identifies a message. If a message has none, use a hash of its content.
std::string Mailbox::keyFor(const MailMessage& msg, const std::string& raw) {
    if (!msg.messageId.empty()) return msg.messageId;
    return "hash:" + std::to_string(std::hash<std::string>{}(raw));
}

bool Mailbox::save(const std::string& raw) {
    MailMessage msg = MailMessage::parse(raw);
    std::string key = keyFor(msg, raw);
    if (knownKeys_.count(key) > 0) return false;  // already downloaded before

    std::ostringstream name;
    name << std::setw(6) << std::setfill('0') << nextNumber_ << ".eml";
    fs::path finalPath = fs::path(dir_) / name.str();
    fs::path tempPath = finalPath;
    tempPath += ".tmp";

    // Write to a temporary file first, then rename. If the program stops halfway,
    // only the .tmp file is incomplete; no broken .eml file appears in the mailbox.
    {
        std::ofstream out(tempPath, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot write " + tempPath.string());
        out << raw;
        if (!out) throw std::runtime_error("Write failed for " + tempPath.string());
    }
    fs::rename(tempPath, finalPath);

    knownKeys_.insert(key);
    ++nextNumber_;
    return true;
}

std::vector<StoredMessage> Mailbox::loadAll() const {
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (entry.path().extension() == ".eml") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());  // 000001.eml, 000002.eml, ... = oldest first

    std::vector<StoredMessage> result;
    for (const fs::path& path : files) {
        result.push_back({ path.filename().string(), MailMessage::parse(readFile(path)) });
    }
    return result;
}

std::string Mailbox::loadRaw(const std::string& fileName) const {
    return readFile(fs::path(dir_) / fileName);
}