#include <stdlib.h>

#include <string.h>
#include <sockets.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

#include <clog.h>

#include "foxctl.h"




socket_t foxctl_socket = {.fd = -1};
char foxctl_buf[1024];

void foxctl_start(const char *restrict path) {
    foxctl_socket = socket_create_unix((char*)path);
    listen(foxctl_socket.fd, 10);
}

int foxctl_fetch_cmd(int timeout) {
    if (foxctl_socket.fd < 0) return FOXCTL_NONE;
    socket_t client = {0};
    struct pollfd pfd;
    pfd.fd = foxctl_socket.fd;
    pfd.events = POLLIN;

    int ready = poll(&pfd, 1, timeout);

    if (ready < 0) {
        return FOXCTL_NONE; // Error
    } else if (ready == 0) {
        return FOXCTL_NONE; // Timeout
    }

    int ret = FOXCTL_NONE;
    socket_accept(foxctl_socket, &client);
    clog(CLOG_TRACE, "Received connection through foxctl socket");

    memset(foxctl_buf, 0, 1024);

    recv(client.fd, foxctl_buf, 1024, 0);
    clog(CLOG_TRACE, "Foxctl: recv %s", foxctl_buf);

    if (strcmp(foxctl_buf, "SHUTDOWN") == 0) {
        ret = FOXCTL_STOP;
    }
    else if (strcmp(foxctl_buf, "RELOAD") == 0) {
        ret = FOXCTL_RELOAD;
    }

    close(client.fd);
    free(client.address);
    clog(CLOG_TRACE, "Connection to foxctl client closed");
    return ret;
}

void foxctl_stop() {
    close(foxctl_socket.fd);
}
