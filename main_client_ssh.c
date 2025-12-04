// =======================================
// main_client_ssh.c  (Checkpoint EX)
// Chat Client with SSH Direct TCP Forwarding
// =======================================

#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define CHAT_PORT 1004

ssh_session sshSess;
ssh_channel sshChan;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// -------------------------------------------
// Thread: listen for messages from SSH channel
// -------------------------------------------
void *reader_thread(void *arg) {
    char buffer[1024];
    int n;

    while ((n = ssh_channel_read(sshChan, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }

    return NULL;
}

// -------------------------------------------
// MAIN
// Usage: ./main_client_ssh <ssh_user> <ssh_pass> <ssh_host> <room>
// Example:
//      ./main_client_ssh khusi mypass 147.26.xx.xx new
// -------------------------------------------
int main(int argc, char *argv[]) {

    if (argc < 5) {
        printf("Usage: %s <ssh_user> <ssh_pass> <ssh_host> <room>\n", argv[0]);
        exit(1);
    }

    char *ssh_user = argv[1];
    char *ssh_pass = argv[2];
    char *ssh_host = argv[3];
    char *room_req = argv[4];

    // ---------------------------
    // 1. Establish SSH SESSION
    // ---------------------------
    sshSess = ssh_new();
    if (!sshSess) exit(1);

    ssh_options_set(sshSess, SSH_OPTIONS_HOST, ssh_host);
    ssh_options_set(sshSess, SSH_OPTIONS_USER, ssh_user);

    if (ssh_connect(sshSess) != SSH_OK) {
        fprintf(stderr, "SSH connection failed: %s\n", ssh_get_error(sshSess));
        exit(1);
    }

    if (ssh_userauth_password(sshSess, NULL, ssh_pass) != SSH_AUTH_SUCCESS) {
        fprintf(stderr, "SSH auth failed: %s\n", ssh_get_error(sshSess));
        exit(1);
    }

    printf("[SSH] Connected & authenticated to %s\n", ssh_host);

    // ---------------------------
    // 2. Create SSH forwarding channel
    // ---------------------------
    sshChan = ssh_channel_new(sshSess);
    if (!sshChan) exit(1);

    if (ssh_channel_open_forward(sshChan,
                                 "127.0.0.1", CHAT_PORT,    // Remote endpoint
                                 "127.0.0.1", 0) != SSH_OK) // Local pretend address
    {
        fprintf(stderr, "SSH port forwarding failed: %s\n", ssh_get_error(sshSess));
        exit(1);
    }

    printf("[SSH] Tunnel established → forwarding to 127.0.0.1:%d\n", CHAT_PORT);

    // ---------------------------------------
    // 3. Begin normal chat protocol behaviour
    // ---------------------------------------

    // Send room selection
    ssh_channel_write(sshChan, room_req, strlen(room_req));

    char buffer[1024];
    int n = ssh_channel_read(sshChan, buffer, sizeof(buffer)-1, 0);
    buffer[n] = '\0';
    printf("Connected to chat room: %s\n", buffer);

    // Ask username
    printf("Type your user name: ");
    fflush(stdout);
    char username[64];
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    ssh_channel_write(sshChan, username, strlen(username));

    // Display join messages
    n = ssh_channel_read(sshChan, buffer, sizeof(buffer)-1, 0);
    buffer[n] = '\0';
    printf("%s", buffer);

    // ---------------------------
    // 4. Start thread for incoming messages
    // ---------------------------
    pthread_t tid;
    pthread_create(&tid, NULL, reader_thread, NULL);

    // ---------------------------
    // 5. Main SEND loop
    // ---------------------------
    while (1) {
        char msg[512];
        fgets(msg, sizeof(msg), stdin);

        if (strcmp(msg, "/quit\n") == 0) break;

        ssh_channel_write(sshChan, msg, strlen(msg));
    }

    ssh_channel_close(sshChan);
    ssh_disconnect(sshSess);
    ssh_free(sshSess);
    return 0;
}
