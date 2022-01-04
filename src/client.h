#include <stdbool.h>

typedef struct client {
    int sockfd;
    char *nick;
    char *username;
    char *fullName;
    bool welcomeMessageSent;
} client;
