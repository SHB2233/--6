// 基础 TCP 服务器：一对一通信示例。
#include "tcp_basic_common.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    tcpdemo::configureConsole();
    unsigned short port = 8888;
    if (argc > 1 && !tcpdemo::parsePort(argv[1], port)) {
        std::cerr << "Port must contain only digits and be in the range 1-65535.\n";
        return 1;
    }
    tcpdemo::NetworkRuntime network;
    if (!network.ready()) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == InvalidSocket) {
        std::cerr << "socket failed (error " << tcpdemo::socketError() << ").\n";
        return 1;
    }
    int reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed (error " << tcpdemo::socketError() << ").\n";
        tcpdemo::closeSocket(listener);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0
        || listen(listener, 1) < 0) {
        std::cerr << "bind/listen failed (error " << tcpdemo::socketError()
                  << "). Check whether the port is already in use.\n";
        tcpdemo::closeSocket(listener);
        return 1;
    }
    std::cout << "Basic TCP server listening on port " << port << ".\n" << std::flush;

    sockaddr_in clientAddress{};
#ifdef _WIN32
    int clientAddressLength = sizeof(clientAddress);
#else
    socklen_t clientAddressLength = sizeof(clientAddress);
#endif
    Socket client = accept(listener, reinterpret_cast<sockaddr *>(&clientAddress),
                           &clientAddressLength);
    if (client == InvalidSocket) {
        std::cerr << "accept failed (error " << tcpdemo::socketError() << ").\n";
        tcpdemo::closeSocket(listener);
        return 1;
    }
    std::cout << "Client connected. Enter a reply after each received message.\n" << std::flush;

    int exitCode = 0;
    std::string message;
    while (true) {
        const auto result = tcpdemo::receiveFrame(client, message);
        if (result != tcpdemo::ReceiveResult::Success) {
            if (result == tcpdemo::ReceiveResult::Closed)
                std::cout << tcpdemo::receiveResultText(result) << '\n';
            else {
                std::cerr << tcpdemo::receiveResultText(result);
                if (result == tcpdemo::ReceiveResult::SocketError)
                    std::cerr << " (error " << tcpdemo::socketError() << ')';
                std::cerr << '\n';
                exitCode = 1;
            }
            break;
        }
        std::cout << "Client: " << message << '\n';
        if (message == "/quit") {
            if (!tcpdemo::sendFrame(client, "The server closed the session.")) {
                std::cerr << "send failed (error " << tcpdemo::socketError() << ").\n";
                exitCode = 1;
            }
            break;
        }
        std::string reply;
        while (true) {
            std::cout << "Server reply> " << std::flush;
            if (!std::getline(std::cin, reply)) {
                std::cout << "Input ended; closing the session.\n";
                break;
            }
            if (reply.empty()) {
                std::cout << "Empty input skipped; enter a reply.\n";
                continue;
            }
            if (reply.size() > 1024 * 1024) {
                std::cout << "Message exceeds 1048576 bytes; enter a shorter reply.\n";
                continue;
            }
            break;
        }
        if (!std::cin)
            break;
        if (!tcpdemo::sendFrame(client, reply)) {
            std::cerr << "send failed (error " << tcpdemo::socketError() << ").\n";
            exitCode = 1;
            break;
        }
    }

    tcpdemo::closeSocket(client);
    tcpdemo::closeSocket(listener);
    return exitCode;
}
