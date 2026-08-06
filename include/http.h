#ifndef __HTTP_H
#define __HTTP_H

#include <stddef.h>
#include <time.h>

enum {
    REQUEST_GET = 0,
    REQUEST_HEAD,
    REQUEST_POST,
    REQUEST_PUT,
    REQUEST_DELETE,
    REQUEST_CONNECT,
    REQUEST_OPTIONS,
    REQUEST_TRACE,
    REQUEST_PATCH,
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

// Defaults to HTTP_CONNECTION_KEEP_ALIVE (value 0) since that's HTTP/1.1's
// default when no `Connection` header is present.
enum {
    HTTP_CONNECTION_KEEP_ALIVE = 0,
    HTTP_CONNECTION_CLOSE,
};

typedef struct {
    int method;
    char *path;
    int version;
    int connection;
} http_request_t;

typedef struct {
    int code;
    int mime_type;
    char *reason;
    char *content;
    time_t date;
} http_response_t;

http_request_t http_request_parse(char *buffer, size_t len);
void http_send_response(int fd, http_response_t response);

int http_method_from_str(const char *str, size_t len);
const char *http_method_to_str(int method);

int mime_type_from_ext(const char *ext, size_t len);
const char *mime_type_str(int mime_type);

#endif //__HTTP_H
