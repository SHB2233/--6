#ifndef TCP_BASIC_COMMON_H
#define TCP_BASIC_COMMON_H

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
const Socket InvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
const Socket InvalidSocket = -1;
#endif

namespace tcpdemo {

class NetworkRuntime {
public:
    NetworkRuntime()
    {
#ifdef _WIN32
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready_ = true;
#endif
    }

    ~NetworkRuntime()
    {
#ifdef _WIN32
        if (ready_)
            WSACleanup();
#endif
    }

    bool ready() const { return ready_; }

private:
    bool ready_ = false;
};

inline void closeSocket(Socket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

inline bool sendAll(Socket socket, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
#ifdef _WIN32
        const int count = send(socket, data + sent,
                               static_cast<int>(length - sent), 0);
#else
        const ssize_t count = send(socket, data + sent, length - sent, 0);
#endif
        if (count <= 0)
            return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

inline bool receiveAll(Socket socket, char *data, size_t length)
{
    size_t received = 0;
    while (received < length) {
#ifdef _WIN32
        const int count = recv(socket, data + received,
                               static_cast<int>(length - received), 0);
#else
        const ssize_t count = recv(socket, data + received, length - received, 0);
#endif
        if (count <= 0)
            return false;
        received += static_cast<size_t>(count);
    }
    return true;
}

inline bool sendFrame(Socket socket, const std::string &message)
{
    constexpr uint32_t MaxMessageBytes = 1024 * 1024;
    if (message.empty() || message.size() > MaxMessageBytes)
        return false;
    const uint32_t networkLength = htonl(static_cast<uint32_t>(message.size()));
    return sendAll(socket, reinterpret_cast<const char *>(&networkLength),
                   sizeof(networkLength))
        && sendAll(socket, message.data(), message.size());
}

inline bool receiveFrame(Socket socket, std::string &message)
{
    constexpr uint32_t MaxMessageBytes = 1024 * 1024;
    uint32_t networkLength = 0;
    if (!receiveAll(socket, reinterpret_cast<char *>(&networkLength),
                    sizeof(networkLength)))
        return false;
    const uint32_t length = ntohl(networkLength);
    if (length == 0 || length > MaxMessageBytes)
        return false;
    message.resize(length);
    return receiveAll(socket, &message[0], message.size());
}

} // namespace tcpdemo

#endif // TCP_BASIC_COMMON_H