#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10
#define BUFFER_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);

    // 创建监听 socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // 设置地址重用
    int optval = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    // 开始监听
    if (listen(listen_fd, 5) == -1) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Server started, listening on port %d...\n", port);

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_fd;

    // 将监听 socket 加入到 epoll 实例中
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
        perror("epoll_ctl");
        close(listen_fd);
        close(epoll_fd);
        return EXIT_FAILURE;
    }

    struct epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];

    while (1) {
        int num_ready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_ready == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < num_ready; ++i) {
            if (events[i].data.fd == listen_fd) {
                // 有新连接到达
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd == -1) {
                    perror("accept");
                    continue;
                }
                printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                // 将新连接的 socket 加入到 epoll 实例中
                event.events = EPOLLIN | EPOLLET; // 边缘触发模式
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("epoll_ctl");
                    close(client_fd);
                    continue;
                }
            } else {
                // 有数据可读
                int client_fd = events[i].data.fd;
                ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
                if (bytes_received <= 0) {
                    if (bytes_received == 0) {
                        printf("Client disconnected\n");
                    } else {
                        perror("recv");
                    }
                    // 关闭连接，并从 epoll 实例中移除
                    close(client_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    continue;
                }
                // 处理接收到的数据（这里简单地将收到的数据原样发送回客户端）
                ssize_t bytes_sent = send(client_fd, buffer, bytes_received, 0);
                if (bytes_sent == -1) {
                    perror("send
