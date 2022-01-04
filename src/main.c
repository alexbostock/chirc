/*
 *
 *  chirc: a simple multi-threaded IRC server
 *
 *  This module provides the main() function for the server,
 *  and parses the command-line arguments to the chirc executable.
 *
 */

/*
 *  Copyright (c) 2011-2020, The University of Chicago
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or withsend
 *  modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of The University of Chicago nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software withsend specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY send OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdbool.h>

#include <pthread.h>

#include "log.h"
#include "client.h"
#include "reply.h"
#include "message.c"

void send_data(client *c, char *data) {
    write(c->sockfd, data, strlen(data));
}

void send_welcome_message(client *c) {
    // <s_host> <RPL_WELCOME> <nick> :Welcome to the Internet Relay Network <username>!<fullName>@<c_host>
    send_data(c, ":irc.alexbostock.co.uk ");
    send_data(c, RPL_WELCOME);
    send_data(c, " ");
    send_data(c, c->nick);
    send_data(c, " :Welcome to the Internet Relay Network ");
    send_data(c, c->nick);
    send_data(c, "!");
    send_data(c, c->username);
    send_data(c, "@");
    send_data(c, "foo.example.com\r\n"); // TODO: get client's hostname
    c->welcomeMessageSent = true;
}

void process_message(char *message, int message_length, client *c) {
    msg *m = parse_message(message, message_length);
    if (strcmp(m->command, "NICK") == 0) {
        chilog(INFO, "Processing NICK");
        c->nick = get_arg(m, 0);
        chilog(INFO, "Parsed nick: %s", c->nick);
    } else if (strcmp(m->command, "USER") == 0) {
        chilog(INFO, "Processing USER");
        c->username = get_arg(m, 0);
        c->fullName = get_arg(m, 3);
        chilog(INFO, "Parsed username: %s", c->username);
        chilog(INFO, "Parsed fullName: %s", c->fullName);
    } else {
        chilog(ERROR, "Unexpected command %s", m->command);
    }
    free_message(m);

    if (c->nick != NULL && c->username != NULL && !c->welcomeMessageSent) {
        send_welcome_message(c);
    }
}

int process_buffered_messages(char *buffer, int buffer_size, int buffer_offset, client *c) {
    int message_start_offset = 0;
    for (int i = 1; i < buffer_offset - 1; i++) {
        if (buffer[i] == '\r' && buffer[i+1] == '\n') {
            int message_length = i - message_start_offset;
            process_message(buffer+message_start_offset, message_length, c);
            message_start_offset = i+2;
        }
    }
    if (message_start_offset == 0 && buffer_offset == buffer_size) {
        chilog(WARNING, "Buffer full of an oversized / invalid message. Dropping buffered data");
        return buffer_size;
    }
    return message_start_offset;
}

void *process_client_messages(void *ptr) {
    client *c = (client *) ptr;
    const int buffer_size = 1024;
    char *buffer = malloc(buffer_size * sizeof(char));
    int buffer_offset = 0;
    while (true) {
        int bytes_read = read(c->sockfd, buffer + buffer_offset, buffer_size - buffer_offset);
        if (bytes_read == -1) {
            chilog(ERROR, "Failed to read from client connection");
            exit(1);
        }
        buffer_offset += bytes_read;
        int consumed_offset = process_buffered_messages(buffer, buffer_size, buffer_offset, c);
        // Assuming that memcpy copies bytes sequential in order (otherwise this might not work)
        memcpy(buffer, buffer + consumed_offset, consumed_offset);
        buffer_offset -= consumed_offset;
    }
}

int main(int argc, char *argv[]) {
    int opt;
    char *port = "6667", *passwd = NULL, *servername = NULL, *network_file = NULL;
    int verbosity = 0;

    while ((opt = getopt(argc, argv, "p:o:s:n:vqh")) != -1)
        switch (opt) {
        case 'p':
            port = strdup(optarg);
            break;
        case 'o':
            passwd = strdup(optarg);
            break;
        case 's':
            servername = strdup(optarg);
            break;
        case 'n':
            if (access(optarg, R_OK) == -1) {
                printf("ERROR: No such file: %s\n", optarg);
                exit(-1);
            }
            network_file = strdup(optarg);
            break;
        case 'v':
            verbosity++;
            break;
        case 'q':
            verbosity = -1;
            break;
        case 'h':
            printf("Usage: chirc -o OPER_PASSWD [-p PORT] [-s SERVERNAME] [-n NETWORK_FILE] [(-q|-v|-vv)]\n");
            exit(0);
            break;
        default:
            fprintf(stderr, "ERROR: Unknown option -%c\n", opt);
            exit(-1);
        }

    if (!passwd) {
        fprintf(stderr, "ERROR: You must specify an operator password\n");
        exit(-1);
    }

    if (network_file && !servername) {
        fprintf(stderr, "ERROR: If specifying a network file, you must also specify a server name.\n");
        exit(-1);
    }

    /* Set logging level based on verbosity */
    switch(verbosity) {
    case -1:
        chirc_setloglevel(QUIET);
        break;
    case 0:
        chirc_setloglevel(INFO);
        break;
    case 1:
        chirc_setloglevel(DEBUG);
        break;
    case 2:
        chirc_setloglevel(TRACE);
        break;
    default:
        chirc_setloglevel(TRACE);
        break;
    }

    uint16_t port_number = atoi(port);
    if (port_number == 0 || port_number > 49151) {
        chilog(CRITICAL, "Invalid port number");
        chilog(CRITICAL, port);
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        chilog(CRITICAL, "Failed to open socket");
        exit(1);
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_number);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        chilog(CRITICAL, "Failed to bind socket");
        exit(1);
    }
    listen(sockfd, 5);
    chilog(INFO, "Listening on port:");
    chilog(INFO, port);

    while (1) {
        // Receive incoming connections

        client *c = malloc(sizeof(client));
        c->nick = NULL;
        c->username = NULL;
        c->fullName = NULL;
        c->welcomeMessageSent = false;

        struct sockaddr client_addr;    // TODO: look at moving this to the heap
        socklen_t client_addr_len;
        c->sockfd = accept(sockfd, &client_addr, &client_addr_len);
        if (c->sockfd == -1) {
            chilog(ERROR, "Failed to accept incoming connection");
            exit(1);
        }

        pthread_t client_thread;
        int iret = pthread_create(&client_thread, NULL, &process_client_messages, c);
    }
}
