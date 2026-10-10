// Logger.cpp
#include "Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {
    std::string gPath = "logs/client.log";
    std::mutex gMutex;  // safe to call from several threads (useful later for a GUI or background checking)

    std::string timestamp() {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::ostringstream out;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return out.str();
    }
}  // namespace

void Logger::init(const std::string& path) {
    std::lock_guard<std::mutex> lock(gMutex);
    gPath = path;
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }

void Logger::write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(gMutex);
    std::ofstream file(gPath, std::ios::app);
    if (file) file << timestamp() << " [" << level << "] " << msg << "\n";
}