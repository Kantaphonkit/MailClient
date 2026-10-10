// ProtocolErrors.h
// Error types for the protocol layer (SMTP / POP3).
// NetworkError (in TcpConnection.h) = the connection itself failed.
// ProtocolError = connected fine, but the server replied with an error or something unexpected.
// AuthError     = the server rejected our username / password.
#pragma once

#include <stdexcept>

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AuthError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};