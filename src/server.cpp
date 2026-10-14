
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

constexpr int PORT = 5000;

void handleClient(int client) {
    char buffer[1024];

    ssize_t n = recv(client, buffer, sizeof(buffer) - 1, 0);
    
    if (n < 0) {
        perror("recv");
        close(client);
        return;
    }

    if (n == 0) {
        std::cerr << "Client disconnected without sending data\n";
        close(client);
        return;
    }

    buffer[n] = '\0';
    std::cout << "Received: " << buffer << '\n';

    const char response[] = "OK: diagnostic server reachable\n";
    
    size_t totalSent = 0;
    const size_t responseLength = sizeof(response) - 1;

    while (totalSent < responseLength){
        ssize_t sent = send(
            client,
            response + totalSent,
            responseLength - totalSent,
            MSG_NOSIGNAL
        );

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("send");
            close(client);
            return;
        }

        if (sent == 0) {
            std::cerr << "Socket closed while sending response\n";
            close(client);
            return;
        }

        totalSent += static_cast<size_t>(sent);
    }

    std::cout <<"Diagnostic response sent successfully\n";
    close(client);
}

int main() {
    int server = socket(AF_INET, SOCK_STREAM, 0);

    if (server < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(PORT);

    if (bind(server, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
        perror("bind");
        close(server);
        return 1;
    }

    if (listen(server, 10) < 0) {
        perror("listen");
        close(server);
        return 1;
    }

    std::cout << "Diagnostic server listening on 127.0.0.1:"
              << PORT << '\n';

    while (true) {
        int client = accept(server, nullptr, nullptr);

        if (client < 0) {
            perror("accept");
            continue;
        }

        std::thread(handleClient, client).detach();
    }

    close(server);
    return 0;
}
