#include "server.h"
#include <signal.h>
#include <sys/epoll.h>
#include <workers.h>
#include <sockets.h>
#include <http.h>
#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CLOG_IMPLEMENTATION
#include <clog.h>

#define CPOOL_IMPLEMENTATION
#include <cpool.h>


config_t config;

static bool running = true;

void sigint(int _) {(void)_;running = false;}

int main(void) {
    cpool_t mempool = cpool_init(1024);
    signal(SIGINT, sigint);
    clog_set_fmt("[Foxtails] " CLOG_PRETTY_FMT);

    config = config_parse("foxtails.conf");
    clog_muted_level = config_get_int(config, "server", "log-level");

    int port = config_get_int(config, "server", "port");
    if (!port) port = 8080;

    int num_workers = config_get_int(config, "server", "num-workers");
    if (!num_workers) num_workers = 8;

    int max_connections = config_get_int(config, "server", "max-connections");
    if (!max_connections) max_connections = 512;

    server_t server = server_init(max_connections, num_workers, port);

    http_socket_listen(server.socket, server.workers);
    clog(CLOG_INFO, "Foxtails listening on *:%i", port);

    struct epoll_event events[32] = {0};
    const int maxevents = 32;

    while (running) {
        int num_events = epoll_wait(server.epoll_instance, events, maxevents, -1);
        if (num_events < 0) {
            if (errno == EINTR) continue;
            clog(CLOG_ERROR, "epoll_wait failed: %s", strerror(errno));
            continue;
        }

        for (int i = 0; i < num_events; i++) {
            if (events[i].data.ptr) {
                client_t *client = events[i].data.ptr;
                clog(CLOG_DEBUG, "Data ready on fd=%d (slot %d). Dispatching to worker", client->socket.fd, client->idx);
                dispatch_client(&server.workers, client);
            } else {
                socket_t client;
                while (http_socket_accept(server.socket, &client)) {
                    server_append_client(&server, client);
                }
            }
        }
    }

    server_delete(server);
    config_delete(config);
    cpool_uninit(mempool);
    return 0;
}
