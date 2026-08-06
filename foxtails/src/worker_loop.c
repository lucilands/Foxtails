#include "config.h"
#include "server.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <time.h>
#include <workers.h>
#include <http.h>
#include <sockets.h>
#include <clog.h>
#include <cpool.h>

#include <pthread.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <limits.h>


extern config_t config;

#define NOT_IMPLEMENTED     (http_response_t) { .code = 501, .mime_type = MIME_TEXT_PLAIN, .reason = "Not Implemented", .content = "Not Implemented" }
#define NOT_FOUND     (http_response_t) { .code = 404, .mime_type = MIME_TEXT_PLAIN, .reason = "Not Found", .content = "Not Found" }



int get_mime_type(const char *path) {
    char *path_cpy = pstrdup((char*)path);
    
     const char *dot = strrchr(path_cpy, '.');
     if (!dot || dot == path_cpy) return 0;

     return mime_type_from_ext(dot+1, strlen(dot+1));
}

int verify_url(const char *approot, const char *full_path) {
    char root_real[PATH_MAX];
    if (!realpath(approot, root_real)) {
        clog(CLOG_ERROR, "Failed to resolve app.root '%s'", approot);
        return 1;
    }

    char full_real[PATH_MAX];
    if (!realpath(full_path, full_real)) {
        clog(CLOG_DEBUG, "Failed to resolve requested path '%s'", full_path);
        return 1;
    }

    size_t root_len = strlen(root_real);
    if (strncmp(full_real, root_real, root_len) != 0) {
        clog(CLOG_WARNING, "Path traversal attempt: '%s' escapes app.root", full_path);
        return 1;
    }

    if (full_real[root_len] != '/' && full_real[root_len] != '\0') {
        clog(CLOG_WARNING, "Path traversal attempt: '%s' escapes app.root", full_path);
        return 1;
    }

    return 0;
}

http_response_t get_path(const char *url) {
    http_response_t resp = NOT_FOUND;

    char *approot = config_get(config, "app", "root");
    if (!approot) {
        return (http_response_t) {
            .code = 500,
            .mime_type = MIME_TEXT_PLAIN,
            .reason = "Internal Server Error",
            .content = "Internal Server Error (app.root key could not be found in config)"
        };
    }

    char *full_path = palloc(strlen(approot) + strlen(url) + 1);
    memset(full_path, 0, strlen(approot) + strlen(url) + 1);
    strcat(full_path, approot);
    strcat(full_path, url);

    if (verify_url(approot, full_path)) {
        free(approot);
        return NOT_FOUND;
    }

    free(approot);


    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        return NOT_FOUND;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return NOT_FOUND;
    }

    if (sb.st_size == 0) {
        close(fd);
        return NOT_FOUND;
    }

    char *content = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (content == MAP_FAILED) {
        close(fd);
        return NOT_FOUND;
    }
    
    resp.code = 200;
    resp.reason = "OK";
    resp.mime_type = get_mime_type(full_path);
    resp.content = palloc(sb.st_size+1);
    memcpy(resp.content, content, sb.st_size);
    resp.content[sb.st_size] = '\0';

    munmap(content, sb.st_size);
    close(fd);
    return resp;
}

http_response_t fetch_response(http_request_t req) {
    clog(CLOG_INFO, "%s %s", http_method_to_str(req.method), req.path);

    switch (req.method) {
        case REQUEST_GET: {
                http_response_t resp = get_path(req.path);
                resp.date = time(NULL);
                return resp;
            }
        case REQUEST_HEAD:
            break;
        case REQUEST_POST:
            break;
        case REQUEST_PUT:
            break;
        case REQUEST_DELETE:
            break;
        case REQUEST_CONNECT:
            break;
        case REQUEST_OPTIONS:
            break;
        case REQUEST_TRACE:
            break;
        case REQUEST_PATCH:
            break;

        default: clog_assert_m(0, "UNREACHABLE");
    }

    return NOT_IMPLEMENTED;
}

void worker_callback(void *payload, int type) {
    switch (type) {
        case WORKER_ACTION_NOOP: break;
        case WORKER_ACTION_NEW_CLIENT: {
            client_t *client = payload;
            char buf[1024];
            int len = recv(client->socket.fd, buf, 1024, 0);
            if (len < 0) {
                clog(CLOG_WARNING, "recv failed on fd=%d (slot %d): %s", client->socket.fd, client->idx, strerror(errno));
                server_remove_client(client->serv, *client);
                break;
            }
            if (len == 0) {
                clog(CLOG_DEBUG, "Client closed connection (fd=%d, slot %d)", client->socket.fd, client->idx);
                server_remove_client(client->serv, *client);
                break;
            }

            http_request_t req = http_request_parse(buf, len);
            http_response_t response = fetch_response(req);

            http_send_response(client->socket.fd, response);

            free(req.path);

            if (req.connection == HTTP_CONNECTION_KEEP_ALIVE) {
                struct epoll_event event = {
                    .events = EPOLLIN | EPOLLONESHOT,
                    .data = {.ptr = &client->serv->clients[client->idx]}
                };
                if (epoll_ctl(client->serv->epoll_instance, EPOLL_CTL_MOD, client->socket.fd, &event)) {
                    clog(CLOG_WARNING, "Failed to re-arm fd=%d (slot %d): %s. Dropping connection", client->socket.fd, client->idx, strerror(errno));
                    server_remove_client(client->serv, *client);
                    break;
                }
                client->serv->clients[client->idx].last_recv = time(NULL);
                clog(CLOG_DEBUG, "Kept connection alive (fd=%d, slot %d)", client->socket.fd, client->idx);
                break;
            }

            clog(CLOG_DEBUG, "Closing connection (fd=%d, slot %d)", client->socket.fd, client->idx);
            server_remove_client(client->serv, *client);
            break;
        }
        default:
            clog_assert_m(0, "UNREACHABLE");
    }
}
