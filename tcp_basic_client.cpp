// 基础 TCP 客户端：连接 tcp_basic_server 并发送/接收文字。
#include "tcp_basic_common.h"

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    tcpdemo::configureConsole();
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    unsigned short port = 8888;
    if (argc > 2 && !tcpdemo::parsePort(argv[2], port)) {
        std::cerr << "Port must contain only digits and be in the range 1-65535.\n";
        return 1;
    }
    tcpdemo::NetworkRuntime network;
    if (!network.ready()) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    Socket client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == InvalidSocket) {
        std::cerr << "socket failed (error " << tcpdemo::socketError() << ").\n";
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << host << '\n';
        tcpdemo::closeSocket(client);
        return 1;
    }
    if (connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        std::cerr << "connect failed (error " << tcpdemo::socketError()
                  << "). Check server IP, port and firewall.\n";
        tcpdemo::closeSocket(client);
        return 1;
    }

    std::cout << "Connected to " << host << ':' << port
              << ". Type /quit to end the session.\n" << std::flush;
    int exitCode = 0;
    std::string message;
    while (true) {
        if (!std::getline(std::cin, message)) {
            std::cout << "Input ended; closing the session.\n";
            break;
        }
        if (message.empty()) {
            std::cout << "Empty input skipped; enter text or /quit.\n";
            continue;
        }
        if (message.size() > 1024 * 1024) {
            std::cout << "Message exceeds 1048576 bytes; enter a shorter message.\n";
            continue;
        }
        if (!tcpdemo::sendFrame(client, message)) {
            std::cerr << "send failed (error " << tcpdemo::socketError() << ").\n";
            exitCode = 1;
            break;
        }
        std::string reply;
        const auto result = tcpdemo::receiveFrame(client, reply);
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
        std::cout << "Server: " << reply << '\n' << std::flush;
        if (message == "/quit")
            break;
    }

    tcpdemo::closeSocket(client);
    return exitCode;
}
