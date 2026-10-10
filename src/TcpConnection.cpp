// TcpConnection.cpp
#include "TcpConnection.h"

#include <cstring>

#ifdef _WIN32
#define CLOSE_SOCKET closesocket
static int lastSocketError() { return WSAGetLastError(); }
static bool wouldBlock(int err) { return err == WSAEWOULDBLOCK; }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET ::close
static int lastSocketError() { return errno; }
static bool wouldBlock(int err) { return err == EINPROGRESS || err == EWOULDBLOCK; }
#endif

namespace {

    constexpr size_t kMaxLineLength = 1 << 20;  // 1 MB: protects against a server that never sends CRLF

    void setNonBlocking(socket_t s, bool enable) {
#ifdef _WIN32
        u_long mode = enable ? 1 : 0;
        ioctlsocket(s, FIONBIO, &mode);
#else
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
    }

    timeval toTimeval(int ms) {
        timeval tv{};
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        return tv;
    }

    // Connect one socket with a timeout: non-blocking connect() + select().
    bool connectWithTimeout(socket_t s, const sockaddr* addr, socklen_t len, int timeoutMs) {
        setNonBlocking(s, true);
        int rc = ::connect(s, addr, len);
        if (rc != 0) {
            if (!wouldBlock(lastSocketError())) return false;

            fd_set writeSet, errorSet;
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(s, &writeSet);
            FD_SET(s, &errorSet);
            timeval tv = toTimeval(timeoutMs);
            rc = select(static_cast<int>(s) + 1, nullptr, &writeSet, &errorSet, &tv);
            if (rc <= 0) return false;  // 0 = timeout, <0 = error

            int soError = 0;
            socklen_t optLen = sizeof(soError);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &optLen);
            if (soError != 0) return false;  // e.g. connection refused
        }
        setNonBlocking(s, false);
        return true;
    }

}  // namespace

// ---------------------------------------------------------------- SocketSystem

SocketSystem::SocketSystem() {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw NetworkError("WSAStartup failed");
    }
#endif
}

SocketSystem::~SocketSystem() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// --------------------------------------------------------------- TcpConnection

socket_t TcpConnection::invalidSocket() {
#ifdef _WIN32
    return INVALID_SOCKET;
#else
    return -1;
#endif
}

TcpConnection::~TcpConnection() { close(); }

bool TcpConnection::isOpen() const { return sock_ != invalidSocket(); }

void TcpConnection::close() {
    if (isOpen()) {
        CLOSE_SOCKET(sock_);
        sock_ = invalidSocket();
    }
    buffer_.clear();
}

void TcpConnection::connect(const std::string& host, int port, int timeoutMs) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // allow IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    addrinfo* results = nullptr;
    std::string portStr = std::to_string(port);

    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &results) != 0 || results == nullptr) {
        throw NetworkError("Cannot resolve host '" + host + "'");
    }

    // "localhost" can resolve to ::1 (IPv6) AND 127.0.0.1 (IPv4).
    // hMailServer only listens on IPv4, so we try every address in turn.
    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        socket_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == invalidSocket()) continue;
        if (connectWithTimeout(s, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen), timeoutMs)) {
            sock_ = s;
            break;
        }
        CLOSE_SOCKET(s);
    }
    freeaddrinfo(results);

    if (!isOpen()) {
        throw NetworkError("Cannot connect to " + host + ":" + portStr +
            " (server down, wrong port, or timeout)");
    }
}

void TcpConnection::sendAll(const std::string& data) {
    if (!isOpen()) throw NetworkError("Not connected");
    size_t sent = 0;
    while (sent < data.size()) {
        int n = ::send(sock_, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0) {
            close();
            throw NetworkError("Connection lost while sending");
        }
        sent += static_cast<size_t>(n);
    }
}

void TcpConnection::sendLine(const std::string& line) { sendAll(line + "\r\n"); }

void TcpConnection::fillBuffer() {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sock_, &readSet);
    timeval tv = toTimeval(readTimeoutMs_);

    int rc = select(static_cast<int>(sock_) + 1, &readSet, nullptr, nullptr, &tv);
    if (rc == 0) {
        throw NetworkError("Timeout: no reply from server within " +
            std::to_string(readTimeoutMs_ / 1000) + " s");
    }
    if (rc < 0) {
        close();
        throw NetworkError("Socket error while waiting for data");
    }

    char chunk[4096];
    int n = ::recv(sock_, chunk, sizeof(chunk), 0);
    if (n == 0) {
        close();
        throw NetworkError("Connection closed by server");
    }
    if (n < 0) {
        close();
        throw NetworkError("Connection lost while receiving");
    }
    buffer_.append(chunk, static_cast<size_t>(n));
}

std::string TcpConnection::readLine() {
    if (!isOpen()) throw NetworkError("Not connected");

    // Keep receiving until the buffer holds at least one full line.
    size_t pos;
    while ((pos = buffer_.find('\n')) == std::string::npos) {
        if (buffer_.size() > kMaxLineLength) {
            close();
            throw NetworkError("Line from server too long");
        }
        fillBuffer();
    }

    std::string line = buffer_.substr(0, pos);
    buffer_.erase(0, pos + 1);
    if (!line.empty() && line.back() == '\r') line.pop_back();  // strip CR of CRLF
    return line;
}