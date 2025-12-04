// =============================
// main_client.c (Checkpoint 4)
// =============================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int running = 1;

void *receive_thread(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[512];

    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) {
            running = 0;
            break;
        }
        printf("%s", buffer);
        fflush(stdout);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: ./main_client <server-ip> <new | room_number>\n");
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(1004);
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    // Send "new" or room #
    send(sockfd, argv[2], strlen(argv[2]), 0);

    // Receive assigned room
    char roomBuf[32];
    memset(roomBuf, 0, sizeof(roomBuf));
    recv(sockfd, roomBuf, sizeof(roomBuf)-1, 0);

    printf("Connected to %s with room number %s\n", argv[1], roomBuf);

    // Username
    char username[64];
    printf("Type your user name: ");
    fflush(stdout);
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    send(sockfd, username, strlen(username), 0);

    // Start receiver thread
    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, &sockfd);

    // MAIN send loop
    char msg[512];
    while (running) {
        memset(msg, 0, sizeof(msg));
        fgets(msg, sizeof(msg), stdin);
        msg[strcspn(msg, "\n")] = 0;

        // /exit — graceful termination
        if (strcmp(msg, "/exit") == 0) {
            send(sockfd, msg, strlen(msg), 0);
            running = 0;
            break;
        }

        send(sockfd, msg, strlen(msg), 0);
    }

    close(sockfd);
    printf("You have exited the chat.\n");
    return 0;
}
