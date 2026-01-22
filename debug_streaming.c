#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("Connecting to server...\n");
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }
    printf("Connected!\n");

    // Test 1: Small SET (1KB)
    printf("\n=== Test 1: 1KB SET ===\n");
    char cmd1[128];
    snprintf(cmd1, sizeof(cmd1), "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$1024\r\n");
    char data1[1024];
    memset(data1, 'A', 1024);

    send(sockfd, cmd1, strlen(cmd1), 0);
    send(sockfd, data1, 1024, 0);
    send(sockfd, "\r\n", 2, 0);

    char resp1[1024];
    ssize_t n = recv(sockfd, resp1, sizeof(resp1), 0);
    if (n > 0) {
        resp1[n] = '\0';
        printf("Response: %s\n", resp1);
    }

    sleep(1);

    // Test 2: Large SET (1MB)
    printf("\n=== Test 2: 1MB SET ===\n");
    size_t mb1 = 1024 * 1024;
    char* data2 = malloc(mb1);
    if (!data2) {
        perror("malloc 1MB");
        close(sockfd);
        return 1;
    }
    memset(data2, 'B', mb1);

    // Build RESP: *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$1048576\r\n
    char cmd2[256];
    snprintf(cmd2, sizeof(cmd2), "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$1048576\r\n");

    printf("Sending command header...\n");
    send(sockfd, cmd2, strlen(cmd2), 0);

    printf("Sending 1MB data in chunks...\n");
    size_t sent = 0;
    while (sent < mb1) {
        ssize_t chunk = 128 * 1024;
        if (sent + chunk > mb1) chunk = mb1 - sent;
        printf("  Sending chunk: %zu bytes\n", chunk);
        ssize_t n = send(sockfd, data2 + sent, chunk, 0);
        if (n < 0) {
            perror("send");
            free(data2);
            close(sockfd);
            return 1;
        }
        sent += n;
        printf("  Sent total: %zu/%zu bytes\n", sent, mb1);
    }

    send(sockfd, "\r\n", 2, 0);
    printf("All data sent! Waiting for response...\n");

    // Receive response
    char resp2[1024];
    printf("Receiving response...\n");
    n = recv(sockfd, resp2, sizeof(resp2), 0);
    if (n > 0) {
        resp2[n] = '\0';
        printf("Response: %s\n", resp2);
    } else {
        printf("No response received (n=%zd)\n", n);
    }

    free(data2);
    close(sockfd);
    printf("\n=== Tests complete ===\n");
    return 0;
}
