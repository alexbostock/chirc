typedef struct msg {
    char *command;
    char **args;
    int numArgs;
} msg;

msg *parse_message(char *message_str, int message_length) {
    // First pass: count arguments
    msg *m = malloc(sizeof(msg));
    m->numArgs = 0;
    for (int offset = 0; offset < message_length; offset++) {
        if (message_str[offset] == ' ') {
            m->numArgs++;
        } else if (message_str[offset] == ':') {
            break;
        }
    }
    m->args = malloc(m->numArgs * sizeof(char *));
    // Second pass: collect arguments
    int offset = 0;
    for (int arg_number = -1; arg_number < m->numArgs; arg_number++) {
        int start_offset = offset;
        for (; offset < message_length; offset++) {
            if (message_str[offset] == ':') {
                start_offset++;
                offset = message_length + 1;
                break;
            } else if (message_str[offset] == ' ') {
                offset++;
                break;
            }
        }
        if (offset == message_length) {
            offset++;
        }
        int arg_len = offset - start_offset;    // Including null-terminator
        chilog(DEBUG, "%i - %i: %i", start_offset, offset, arg_len);
        char *arg = malloc(arg_len * sizeof(char));
        memcpy(arg, &message_str[start_offset], arg_len - 1);
        arg[arg_len - 1] = '\0';
        chilog(DEBUG, "Arg: %s", arg);
        if (arg_number == -1) {
            m->command = arg;
        } else {
            m->args[arg_number] = arg;
        }
    }
    return m;
}

// Args should only be fetched using get_arg, and must not be fetched more than once
char *get_arg(msg *m, int arg_index) {
    char *arg = m->args[arg_index];
    m->args[arg_index] = NULL;      // Used to avoid freeing memory used by caller
    return arg;
}

void free_message(msg *m) {
    for (int i = 0; i < m->numArgs; i++) {
        if (m->args[i] != NULL) {
            free(m->args[i]);
        }
    }
    free(m->args);
    free(m);
}
