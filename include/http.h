#ifndef __HTTP_H
#define __HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

enum {
    REQUEST_GET     = 1 << 0,
    REQUEST_HEAD    = 1 << 1,
    REQUEST_POST    = 1 << 2,
    REQUEST_PUT     = 1 << 3,
    REQUEST_DELETE  = 1 << 4,
    REQUEST_CONNECT = 1 << 5,
    REQUEST_OPTIONS = 1 << 6,
    REQUEST_TRACE   = 1 << 7,
    REQUEST_PATCH   = 1 << 8,
};

enum {
    REQUEST_ALL = REQUEST_GET | REQUEST_HEAD | REQUEST_POST | REQUEST_PUT | REQUEST_DELETE
                | REQUEST_CONNECT | REQUEST_OPTIONS | REQUEST_TRACE | REQUEST_PATCH,
};

enum {
    MIME_TEXT_PLAIN = 0,
    MIME_TEXT_HTML,
    MIME_TEXT_CSS,
    MIME_TEXT_JAVASCRIPT,
    MIME_APPLICATION_JSON,
    MIME_APPLICATION_XML,
    MIME_APPLICATION_PDF,
    MIME_APPLICATION_OCTET_STREAM,
    MIME_IMAGE_PNG,
    MIME_IMAGE_JPEG,
    MIME_IMAGE_GIF,
    MIME_IMAGE_SVG,
    MIME_IMAGE_ICO,
};

enum {
    HTTP_VERSION_1_1 = 0,
};

enum {
    HTTP_CONNECTION_KEEP_ALIVE = 0,
    HTTP_CONNECTION_CLOSE,
};

#ifndef HTTP_MAX_HEADERS
#define HTTP_MAX_HEADERS 32
#endif //HTTP_MAX_HEADERS

#define NOT_IMPLEMENTED     (http_t) { .code = 501, .reason = "Not Implemented", .body = "Not Implemented", .body_len = sizeof("Not Implemented") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define NOT_FOUND           (http_t) { .code = 404, .reason = "Not Found", .body = "Not Found", .body_len = sizeof("Not Found") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define BAD_REQUEST         (http_t) { .code = 400, .reason = "Bad Request", .body = "Bad Request", .body_len = sizeof("Bad Request") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define NOT_ALLOWED         (http_t) { .code = 405, .reason = "Method Not Allowed", .body = "Method Not Allowed", .body_len = sizeof("Method Not Allowed") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}, {"Allow", "GET, HEAD, POST, PUT, DELETE, PATCH"}}, .len = 2 } }
#define NO_CONTENT          (http_t) { .code = 204, .reason = "No Content", .body = NULL, .body_len = 0, \
                                        .headers = { .items = {{"Allow", "GET, HEAD, POST, PUT, DELETE, PATCH"}}, .len = 1 } }
#define HTTP_MOVED(loc)     (http_t) { .code = 301, .reason = "Moved Permanently", .body = "Moved Permanently", .body_len = sizeof("Moved Permanently") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}, {"Location", (loc)}}, .len = 2 } }
#define NOT_IMPLEMENTED       (http_t) { .code = 501, .reason = "Not Implemented", .body = "Not Implemented", .body_len = sizeof("Not Implemented") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define VERSION_NOT_SUPPORTED (http_t) { .code = 505, .reason = "HTTP Version Not Supported", .body = "HTTP Version Not Supported", .body_len = sizeof("HTTP Version Not Supported") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define REQUEST_TIMEOUT       (http_t) { .code = 408, .reason = "Request Timeout", .body = "Request Timeout", .body_len = sizeof("Request Timeout") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }
#define BAD_GATEWAY           (http_t) { .code = 502, .reason = "Bad Gateway", .body = "Bad Gateway", .body_len = sizeof("Bad Gateway") - 1, \
                                        .headers = { .items = {{"Content-Type", "text/plain"}}, .len = 1 } }


typedef struct {
    char *name;
    char *value;
} http_header_t;

typedef struct {
    http_header_t items[HTTP_MAX_HEADERS];
    size_t len;
} http_headers_t;

typedef struct {
    int method;
    char *path;

    int code;
    char *reason;

    int version;
    http_headers_t headers;

    char *body;
    size_t body_len;
} http_t;

http_t http_request_parse(char *buffer, size_t len);
http_t http_response_parse(char *buffer, size_t len);
char *http_recv_message(int fd, size_t *out_len, bool expect_body);
void http_send_response(int fd, http_t response);
void http_send_request(int fd, http_t request);

bool http_set_header(http_t *msg, const char *name, const char *value);
char *http_get_header(const http_t *msg, const char *name);
bool http_has_header(const http_t *msg, const char *name);
bool http_is_keep_alive(const http_t *msg);

#define http_foreach_header(msg, h) \
    for (http_header_t *h = (msg)->headers.items; h < (msg)->headers.items + (msg)->headers.len; h++)

#define http_set_content_type(msg, mime) http_set_header((msg), "Content-Type", mime_type_str(mime))
#define http_set_location(msg, loc)      http_set_header((msg), "Location", (loc))

int http_method_from_str(const char *str, size_t len);
const char *http_method_to_str(int method);

int mime_type_from_ext(const char *ext, size_t len);
const char *mime_type_str(int mime_type);

#endif //__HTTP_H
