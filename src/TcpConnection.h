// TcpConnection.h
// A small wrapper around a TCP socket (Winsock on Windows, BSD sockets elsewhere).
// TCP is a byte stream, so we keep our own buffer and hand out complete lines.
#pragma once

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
using socket_t = int;
#endif

// Thrown for any transport problem: cannot connect, timeout, connection closed.
class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Winsock must be started once per program (WSAStartup) and cleaned up at exit.
// Create one SocketSystem object at the top of main(). On Linux it does nothing.
class SocketSystem {
public:
    SocketSystem();
    ~SocketSystem();
    SocketSystem(const SocketSystem&) = delete;
    SocketSystem& operator=(const SocketSystem&) = delete;
};

class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // Resolve host (IPv4 and IPv6) and try each address until one connects.
    void connect(const std::string& host, int port, int timeoutMs = 5000);

    // Send every byte of data (send() may send only part, so we loop).
    void sendAll(const std::string& data);

    // Send one protocol line; CRLF is appended here.
    void sendLine(const std::string& line);

    // Return the next complete line WITHOUT the trailing CRLF.
    std::string readLine();

    void setReadTimeout(int ms) { readTimeoutMs_ = ms; }
    bool isOpen() const;
    void close();

private:
    // Wait until the socket has data (or timeout), then recv() into buffer_.
    void fillBuffer();

    socket_t sock_ = invalidSocket();
    std::string buffer_;          // bytes received but not yet returned as lines
    int readTimeoutMs_ = 10000;

    static socket_t invalidSocket();
};