#include <http.h>

#include <clog.h>
#include <cpool.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>


int http_method_from_str(const char *str, size_t len) {
    switch (len) {
        case 3:
            if (memcmp(str, "GET", 3) == 0) return REQUEST_GET;
            if (memcmp(str, "PUT", 3) == 0) return REQUEST_PUT;
            break;
        case 4:
            if (memcmp(str, "HEAD", 4) == 0) return REQUEST_HEAD;
            if (memcmp(str, "POST", 4) == 0) return REQUEST_POST;
            break;
        case 5:
            if (memcmp(str, "PATCH", 5) == 0) return REQUEST_PATCH;
            if (memcmp(str, "TRACE", 5) == 0) return REQUEST_TRACE;
            break;
        case 6:
            if (memcmp(str, "DELETE", 6) == 0) return REQUEST_DELETE;
            break;
        case 7:
            if (memcmp(str, "OPTIONS", 7) == 0) return REQUEST_OPTIONS;
            if (memcmp(str, "CONNECT", 7) == 0) return REQUEST_CONNECT;
            break;
    }
    return -1;
}

const char *http_method_to_str(int method) {
    switch (method) {
        case REQUEST_GET:     return "GET";
        case REQUEST_HEAD:    return "HEAD";
        case REQUEST_POST:    return "POST";
        case REQUEST_PUT:     return "PUT";
        case REQUEST_DELETE:  return "DELETE";
        case REQUEST_CONNECT: return "CONNECT";
        case REQUEST_OPTIONS: return "OPTIONS";
        case REQUEST_TRACE:   return "TRACE";
        case REQUEST_PATCH:   return "PATCH";
        default:              return NULL;
    }
}

int mime_type_from_ext(const char *ext, size_t len) {
    switch (len) {
        case 2:
            if (memcmp(ext, "js", 2) == 0) return MIME_TEXT_JAVASCRIPT;
            break;
        case 3:
            if (memcmp(ext, "css", 3) == 0) return MIME_TEXT_CSS;
            if (memcmp(ext, "xml", 3) == 0) return MIME_APPLICATION_XML;
            if (memcmp(ext, "pdf", 3) == 0) return MIME_APPLICATION_PDF;
            if (memcmp(ext, "png", 3) == 0) return MIME_IMAGE_PNG;
            if (memcmp(ext, "jpg", 3) == 0) return MIME_IMAGE_JPEG;
            if (memcmp(ext, "gif", 3) == 0) return MIME_IMAGE_GIF;
            if (memcmp(ext, "svg", 3) == 0) return MIME_IMAGE_SVG;
            if (memcmp(ext, "ico", 3) == 0) return MIME_IMAGE_ICO;
            if (memcmp(ext, "txt", 3) == 0) return MIME_TEXT_PLAIN;
            break;
        case 4:
            if (memcmp(ext, "html", 4) == 0) return MIME_TEXT_HTML;
            if (memcmp(ext, "json", 4) == 0) return MIME_APPLICATION_JSON;
            if (memcmp(ext, "jpeg", 4) == 0) return MIME_IMAGE_JPEG;
            break;
    }
    return MIME_APPLICATION_OCTET_STREAM;
}

const char *mime_type_str(int mime_type) {
    switch (mime_type) {
        case MIME_TEXT_PLAIN:             return "text/plain";
        case MIME_TEXT_HTML:              return "text/html";
        case MIME_TEXT_CSS:               return "text/css";
        case MIME_TEXT_JAVASCRIPT:        return "text/javascript";
        case MIME_APPLICATION_JSON:       return "application/json";
        case MIME_APPLICATION_XML:        return "application/xml";
        case MIME_APPLICATION_PDF:        return "application/pdf";
        case MIME_APPLICATION_OCTET_STREAM: return "application/octet-stream";
        case MIME_IMAGE_PNG:              return "image/png";
        case MIME_IMAGE_JPEG:             return "image/jpeg";
        case MIME_IMAGE_GIF:              return "image/gif";
        case MIME_IMAGE_SVG:              return "image/svg+xml";
        case MIME_IMAGE_ICO:              return "image/x-icon";
        default:                          return "application/octet-stream";
    }
}

bool http_set_header(http_t *msg, const char *name, const char *value) {
    http_foreach_header(msg, h) {
        if (strcasecmp(h->name, name) == 0) {
            h->value = pstrdup((char*)value);
            return true;
        }
    }

    if (msg->headers.len >= HTTP_MAX_HEADERS) {
        clog(CLOG_ERROR, "Dropping header '%s': HTTP_MAX_HEADERS (%d) exceeded", name, HTTP_MAX_HEADERS);
        return false;
    }

    msg->headers.items[msg->headers.len++] = (http_header_t) {
        .name = pstrdup((char*)name),
        .value = pstrdup((char*)value),
    };
    return true;
}

char *http_get_header(const http_t *msg, const char *name) {
    for (size_t i = 0; i < msg->headers.len; i++) {
        if (strcasecmp(msg->headers.items[i].name, name) == 0) {
            return msg->headers.items[i].value;
        }
    }
    return NULL;
}

bool http_has_header(const http_t *msg, const char *name) {
    return http_get_header(msg, name) != NULL;
}

bool http_is_keep_alive(const http_t *msg) {
    char *value = http_get_header(msg, "Connection");
    return !(value && strcasecmp(value, "close") == 0);
}

static char *http_first_line(char *buffer, size_t len, size_t *out_line_len) {
    char *line_end = memchr(buffer, '\n', len);
    if (!line_end) return NULL;

    size_t line_len = line_end - buffer;
    if (line_len > 0 && buffer[line_len - 1] == '\r') {
        line_len--;  /* strip trailing \r */
    }

    *out_line_len = line_len;
    return line_end;
}

static void http_parse_headers_and_body(http_t *msg, char *buffer, size_t len, char *headers_start) {
    size_t headers_remaining = len - (headers_start - buffer);

    char *body_start = NULL;
    char *blank_crlf = memmem(headers_start, headers_remaining, "\r\n\r\n", 4);
    char *blank_lf = memmem(headers_start, headers_remaining, "\n\n", 2);
    char *header_block_end = headers_start + headers_remaining;
    if (blank_crlf && (!blank_lf || blank_crlf <= blank_lf)) {
        header_block_end = blank_crlf;
        body_start = blank_crlf + 4;
    } else if (blank_lf) {
        header_block_end = blank_lf;
        body_start = blank_lf + 2;
    }

    char *line;
    char *header_block = pstrndup(headers_start, header_block_end - headers_start);
    for (line = strtok(header_block, "\n"); line; line = strtok(NULL, "\n")) {
        size_t hlen = strlen(line);
        if (hlen > 0 && line[hlen - 1] == '\r') line[hlen - 1] = '\0';

        char *colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char *value = colon + 1;
        while (*value == ' ') value++;

        http_set_header(msg, line, value);
    }

    char *content_length_str = http_get_header(msg, "Content-Length");
    size_t content_length = content_length_str ? strtoul(content_length_str, NULL, 10) : 0;

    if (content_length > 0 && body_start) {
        size_t available = (buffer + len) - body_start;
        size_t body_len = content_length < available ? content_length : available;
        if (body_len > 0) {
            msg->body = pmemdup(body_start, body_len);
            msg->body_len = body_len;
        }

        if (available < content_length) {
            clog(CLOG_WARNING, "Truncated body: got %zu of %zu bytes (Content-Length)",
                 available, content_length);
        }
    }
}

http_t http_request_parse(char *buf, size_t len) {
    char *buffer = pmemdup(buf, len);

    size_t line_len;
    char *line_end = http_first_line(buffer, len, &line_len);
    if (!line_end) {
        clog(CLOG_WARNING, "Partial request. Ignoring");
        return REQUEST_TIMEOUT;
    }

    http_t request = {0};

    char *method_start = buffer;
    char *method_end = memchr(buffer, ' ', line_len);
    if (!method_end) {
        clog(CLOG_ERROR, "Malformed request line. Ignoring");
        return BAD_REQUEST;
    }
    size_t method_len = method_end - method_start;

    request.method = http_method_from_str(method_start, method_len);
    if (request.method < 0) {
        clog(CLOG_ERROR, "Invalid HTTP method %.*s", (int)method_len, method_start);
        return NOT_IMPLEMENTED;
    }

    char *path_start = method_end + 1;
    size_t path_remaining = line_len - method_len - 1;
    char *path_end = memchr(path_start, ' ', path_remaining);
    if (!path_end) {
        clog(CLOG_ERROR, "Malformed request. Ignoring");
        return BAD_REQUEST;
    }
    size_t path_len = path_end - path_start;

    char *version_start = path_end + 1;
    size_t version_len = line_len - (version_start - buffer);

    if (version_len != 8 || memcmp(version_start, "HTTP/1.1", 8) != 0) {
        clog(CLOG_ERROR, "Unsupported HTTP version %.*s", (int)version_len, version_start);
        return VERSION_NOT_SUPPORTED;
    }
    request.version = HTTP_VERSION_1_1;
    request.path = pstrndup(path_start, path_len);

    http_parse_headers_and_body(&request, buffer, len, line_end + 1);

    return request;
}

http_t http_response_parse(char *buf, size_t len) {
    char *buffer = pmemdup(buf, len);

    size_t line_len;
    char *line_end = http_first_line(buffer, len, &line_len);
    if (!line_end) {
        clog(CLOG_WARNING, "Partial response. Ignoring");
        return BAD_GATEWAY;
    }

    http_t response = {0};

    char *version_start = buffer;
    char *version_end = memchr(buffer, ' ', line_len);
    if (!version_end) {
        clog(CLOG_ERROR, "Malformed status line. Ignoring");
        return BAD_GATEWAY;
    }
    size_t version_len = version_end - version_start;

    if (version_len != 8 || memcmp(version_start, "HTTP/1.1", 8) != 0) {
        clog(CLOG_ERROR, "Unsupported HTTP version %.*s", (int)version_len, version_start);
        return VERSION_NOT_SUPPORTED;
    }
    response.version = HTTP_VERSION_1_1;

    char *code_start = version_end + 1;
    size_t code_remaining = line_len - version_len - 1;
    char *code_end = memchr(code_start, ' ', code_remaining);
    if (!code_end) {
        clog(CLOG_ERROR, "Malformed status line. Ignoring");
        return BAD_GATEWAY;
    }
    size_t code_len = code_end - code_start;

    char code_buf[16];
    if (code_len >= sizeof(code_buf)) code_len = sizeof(code_buf) - 1;
    memcpy(code_buf, code_start, code_len);
    code_buf[code_len] = '\0';
    response.code = atoi(code_buf);

    char *reason_start = code_end + 1;
    size_t reason_len = line_len - (reason_start - buffer);
    response.reason = pstrndup(reason_start, reason_len);

    http_parse_headers_and_body(&response, buffer, len, line_end + 1);

    return response;
}


void http_send_response(int fd, http_t response) {
    char header[4096];
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char date[32];
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", tm_info);

    if (!http_has_header(&response, "Content-Type")) {
        http_set_content_type(&response, MIME_TEXT_PLAIN);
    }

    size_t header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %i %s\r\nDate: %s\r\nServer: Foxtails\r\nContent-Length: %zu\r\n",
        response.code, response.reason, date, response.body_len);

    http_foreach_header(&response, h) {
        header_len += snprintf(header + header_len, sizeof(header) - header_len,
            "%s: %s\r\n", h->name, h->value);
    }

    header_len += snprintf(header + header_len, sizeof(header) - header_len, "\r\n");

    if (!response.body) {
        ssize_t sent = send(fd, header, header_len, 0);
        if (sent < 0) {
            clog(CLOG_WARNING, "Failed to send response on fd=%d: %s", fd, strerror(errno));
        } else {
            clog(CLOG_DEBUG, "Sent %i response (%zu bytes) on fd=%d", response.code, header_len, fd);
        }
        return;
    }

    size_t response_len = header_len + response.body_len;
    char *resp = palloc(response_len + 1);
    if (!resp) {
        clog(CLOG_ERROR, "Failed to allocate memory for response (fd=%d)", fd);
        return;
    }

    memcpy(resp, header, header_len);
    memcpy(resp + header_len, response.body, response.body_len);

    ssize_t sent = send(fd, resp, response_len, 0);
    if (sent < 0) {
        clog(CLOG_WARNING, "Failed to send response on fd=%d: %s", fd, strerror(errno));
    } else {
        clog(CLOG_DEBUG, "Sent %i response (%zu bytes) on fd=%d", response.code, response_len, fd);
    }
}

char *http_recv_message(int fd, size_t *out_len, bool expect_body) {
    size_t cap = 4096, len = 0, content_length = 0;
    char *buf = malloc(cap);
    char *body_start = NULL;
    ssize_t n;

    while ((n = recv(fd, buf + len, cap - len, 0)) > 0) {
        len += n;
        if (!body_start && (body_start = memmem(buf, len, "\r\n\r\n", 4))) {
            body_start += 4;
            if (expect_body) {
                char *cl = memmem(buf, body_start - buf, "Content-Length:", 15);
                if (cl) content_length = strtoul(cl + 15, NULL, 10);
            }
        }
        if (body_start && (size_t)(buf + len - body_start) >= content_length) break;
        if (cap - len < 1024) buf = realloc(buf, cap *= 2);
    }
    *out_len = len;
    return buf;
}

void http_send_request(int fd, http_t request) {
    char header[4096];

    size_t header_len = snprintf(header, sizeof(header),
        "%s %s HTTP/1.1\r\nContent-Length: %zu\r\n",
        http_method_to_str(request.method), request.path, request.body_len);

    http_foreach_header(&request, h) {
        if (strcasecmp(h->name, "Content-Length") == 0) continue;
        header_len += snprintf(header + header_len, sizeof(header) - header_len,
            "%s: %s\r\n", h->name, h->value);
    }

    header_len += snprintf(header + header_len, sizeof(header) - header_len, "\r\n");

    if (!request.body) {
        ssize_t sent = send(fd, header, header_len, 0);
        if (sent < 0) {
            clog(CLOG_WARNING, "Failed to send request on fd=%d: %s", fd, strerror(errno));
        } else {
            clog(CLOG_DEBUG, "Sent %s %s request (%zu bytes) on fd=%d", http_method_to_str(request.method), request.path, header_len, fd);
        }
        return;
    }

    size_t request_len = header_len + request.body_len;
    char *req = palloc(request_len + 1);
    if (!req) {
        clog(CLOG_ERROR, "Failed to allocate memory for request (fd=%d)", fd);
        return;
    }

    memcpy(req, header, header_len);
    memcpy(req + header_len, request.body, request.body_len);

    ssize_t sent = send(fd, req, request_len, 0);
    if (sent < 0) {
        clog(CLOG_WARNING, "Failed to send request on fd=%d: %s", fd, strerror(errno));
    } else {
        clog(CLOG_DEBUG, "Sent %s %s request (%zu bytes) on fd=%d", http_method_to_str(request.method), request.path, request_len, fd);
    }
}
