// 基础 TCP 服务器：一对一通信示例。
#include "tcp_basic_common.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    const int portArg = argc > 1 ? std::stoi(argv[1]) : 8888;
    if (portArg < 1 || portArg > 65535) {
        std::cerr << "Port must be in the range 1-65535.\n";
        return 1;
    }
    const auto port = static_cast<unsigned short>(portArg);
    tcpdemo::NetworkRuntime network;
    if (!network.ready()) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == InvalidSocket) {
        std::cerr << "socket failed.\n";
        return 1;
    }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0
        || listen(listener, 1) < 0) {
        std::cerr << "bind/listen failed. Check whether the port is already in use.\n";
        tcpdemo::closeSocket(listener);
        return 1;
    }
    std::cout << "Basic TCP server listening on port " << port << ".\n";

    sockaddr_in clientAddress{};
#ifdef _WIN32
    int clientAddressLength = sizeof(clientAddress);
#else
    socklen_t clientAddressLength = sizeof(clientAddress);
#endif
    Socket client = accept(listener, reinterpret_cast<sockaddr *>(&clientAddress),
                           &clientAddressLength);
    if (client == InvalidSocket) {
        std::cerr << "accept failed.\n";
        tcpdemo::closeSocket(listener);
        return 1;
    }
    std::cout << "Client connected. Enter a reply after each received message.\n";

    std::string message;
    while (tcpdemo::receiveFrame(client, message)) {
        std::cout << "Client: " << message << '\n';
        if (message == "/quit") {
            tcpdemo::sendFrame(client, "The server closed the session.");
            break;
        }
        std::cout << "Server reply> ";
        std::string reply;
        if (!std::getline(std::cin, reply))
            reply = "Server received your message.";
        if (!tcpdemo::sendFrame(client, reply))
            break;
    }

    tcpdemo::closeSocket(client);
    tcpdemo::closeSocket(listener);
    return 0;
}