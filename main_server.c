// =============================
// main_server.c (Checkpoint 4)
// =============================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PORT_NUM 1004
#define MAX_ROOMS 10

void error(const char *msg) {
    perror(msg);
    exit(1);
}

char *colorList[] = {
    "\033[31m", "\033[32m", "\033[33m", "\033[34m",
    "\033[35m", "\033[36m", "\033[91m", "\033[92m",
    "\033[93m", "\033[94m", "\033[95m", "\033[96m"
};
#define NUM_COLORS (sizeof(colorList)/sizeof(colorList[0]))

// --------------------------------------
// Client structure
// --------------------------------------
typedef struct Client {
    int sockfd;
    char username[64];
    char color[16];
    int room_id;
    struct Client *next;
} Client;

Client *rooms[MAX_ROOMS];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// --------------------------------------
Client* find_client_in_room(int room, const char *username) {
    Client *c = rooms[room];
    while (c) {
        if (strcmp(c->username, username) == 0)
            return c;
        c = c->next;
    }
    return NULL;
}

// --------------------------------------
void add_client_to_room(int room, Client *c) {
    pthread_mutex_lock(&lock);
    c->next = rooms[room];
    rooms[room] = c;
    pthread_mutex_unlock(&lock);
}

// --------------------------------------
void remove_client_from_room(int sockfd, int room) {
    pthread_mutex_lock(&lock);
    Client *cur = rooms[room];
    Client *prev = NULL;

    while (cur) {
        if (cur->sockfd == sockfd) {
            if (prev == NULL) rooms[room] = cur->next;
            else prev->next = cur->next;
            free(cur);
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    pthread_mutex_unlock(&lock);
}

// --------------------------------------
void broadcast_to_room(int room, const char *msg, int except_fd) {
    pthread_mutex_lock(&lock);
    Client *c = rooms[room];
    while (c) {
        if (c->sockfd != except_fd)
            send(c->sockfd, msg, strlen(msg), 0);
        c = c->next;
    }
    pthread_mutex_unlock(&lock);
}

// --------------------------------------
// Client thread
// --------------------------------------
void *client_thread(void *arg) {
    int sockfd = *(int *)arg;
    free(arg);

    char buffer[512];
    memset(buffer, 0, sizeof(buffer));

    // STEP 1: Room request
    recv(sockfd, buffer, sizeof(buffer)-1, 0);

    int room_id;
    if (strcmp(buffer, "new") == 0) {
        room_id = -1;
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (rooms[i] == NULL) {
                room_id = i;
                break;
            }
        }
        if (room_id == -1) {
            close(sockfd);
            return NULL;
        }
        char msg[32];
        sprintf(msg, "%d", room_id);
        send(sockfd, msg, strlen(msg), 0);

    } else {
        room_id = atoi(buffer);
        if (room_id < 0 || room_id >= MAX_ROOMS || rooms[room_id] == NULL) {
            close(sockfd);
            return NULL;
        }
        send(sockfd, buffer, strlen(buffer), 0);
    }

    // STEP 2: Username
    memset(buffer, 0, sizeof(buffer));
    recv(sockfd, buffer, sizeof(buffer)-1, 0);

    Client *c = malloc(sizeof(Client));
    c->sockfd = sockfd;
    strcpy(c->username, buffer);
    strcpy(c->color, colorList[rand() % NUM_COLORS]);
    c->room_id = room_id;
    c->next = NULL;

    add_client_to_room(room_id, c);

    // Announce join
    char joinMsg[256];
    sprintf(joinMsg, "%s%s joined the chat!\033[0m\n", c->color, c->username);
    broadcast_to_room(room_id, joinMsg, -1);

    // STEP 3: Main message loop
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;

        // REMOVE newline
        buffer[strcspn(buffer, "\n")] = 0;

        // --------------------------------------
        // HANDLE /exit
        // --------------------------------------
        if (strcmp(buffer, "/exit") == 0) {
            char leaveMsg[256];
            sprintf(leaveMsg, "%s%s left the chat.\033[0m\n", c->color, c->username);
            broadcast_to_room(room_id, leaveMsg, sockfd);
            break;
        }

        // --------------------------------------
        // Command handling
        // --------------------------------------
        if (buffer[0] == '/') {

            // /list
            if (strncmp(buffer, "/list", 5) == 0) {
                char userList[512] = "Users in this room:\n";

                pthread_mutex_lock(&lock);
                Client *temp = rooms[room_id];
                while (temp) {
                    strcat(userList, "- ");
                    strcat(userList, temp->username);
                    strcat(userList, "\n");
                    temp = temp->next;
                }
                pthread_mutex_unlock(&lock);

                send(sockfd, userList, strlen(userList), 0);
                continue;
            }

            // /rooms
            if (strncmp(buffer, "/rooms", 6) == 0) {
                char roomList[512] = "Active rooms:\n";
                pthread_mutex_lock(&lock);
                for (int i = 0; i < MAX_ROOMS; i++) {
                    if (rooms[i] != NULL) {
                        char tmp[32];
                        sprintf(tmp, "Room %d\n", i);
                        strcat(roomList, tmp);
                    }
                }
                pthread_mutex_unlock(&lock);
                send(sockfd, roomList, strlen(roomList), 0);
                continue;
            }

            // /whisper <user> <msg>
            if (strncmp(buffer, "/whisper", 8) == 0) {
                char target[64], msg[512];
                sscanf(buffer, "/whisper %s %[^\n]", target, msg);

                Client *t = find_client_in_room(room_id, target);

                if (t) {
                    char whisperOut[600];
                    sprintf(whisperOut, "\033[35m[Private from %s]: %s\033[0m\n",
                            c->username, msg);
                    send(t->sockfd, whisperOut, strlen(whisperOut), 0);
                } else {
                    char err[] = "User not found in this room.\n";
                    send(sockfd, err, strlen(err), 0);
                }
                continue;
            }

            // Unknown command
            send(sockfd, "Unknown command.\n", 18, 0);
            continue;
        }

        // NORMAL message
        char msgOut[600];
        sprintf(msgOut, "%s[%s]: %s\033[0m\n", c->color, c->username, buffer);
        broadcast_to_room(room_id, msgOut, sockfd);
    }

    // Cleanup
    remove_client_from_room(sockfd, room_id);
    close(sockfd);
    return NULL;
}

// --------------------------------------
// MAIN SERVER
// --------------------------------------
int main() {
    srand(time(NULL));
    memset(rooms, 0, sizeof(rooms));

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    socklen_t slen = sizeof(serv_addr);

    memset(&serv_addr, 0, slen);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_NUM);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, slen) < 0)
        error("ERROR on binding");

    listen(sockfd, 10);

    printf("Server running on port %d...\n", PORT_NUM);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t clen = sizeof(cli_addr);

        int *newsockfd = malloc(sizeof(int));
        *newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clen);
        if (*newsockfd < 0) error("ERROR on accept");

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, newsockfd);
        pthread_detach(tid);
    }

    return 0;
}
