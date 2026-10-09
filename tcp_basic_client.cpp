// 基础 TCP 客户端：连接 tcp_basic_server 并发送/接收文字。
#include "tcp_basic_common.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const int portArg = argc > 2 ? std::stoi(argv[2]) : 8888;
    if (portArg < 1 || portArg > 65535) {
        std::cerr << "Port must be in the range 1-65535.\n";
        return 1;
    }
    tcpdemo::NetworkRuntime network;
    if (!network.ready()) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    Socket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == InvalidSocket) {
        std::cerr << "socket failed.\n";
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(portArg));
    address.sin_addr.s_addr = inet_addr(host.c_str());
    if (address.sin_addr.s_addr == INADDR_NONE
        || connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        std::cerr << "connect failed. Check server IP, port and firewall.\n";
        tcpdemo::closeSocket(client);
        return 1;
    }

    std::cout << "Connected to " << host << ':' << portArg
              << ". Type /quit to end the session.\n";
    std::string message;
    while (std::getline(std::cin, message)) {
        if (!tcpdemo::sendFrame(client, message))
            break;
        std::string reply;
        if (!tcpdemo::receiveFrame(client, reply))
            break;
        std::cout << "Server: " << reply << '\n';
        if (message == "/quit")
            break;
    }

    tcpdemo::closeSocket(client);
    return 0;
}