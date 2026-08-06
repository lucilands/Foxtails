#include <http.h>

#include <clog.h>

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

http_request_t http_request_parse(char *buf, size_t len) {
    char *buffer = strdup(buf);

    char *line_end = memchr(buffer, '\n', len);
    if (!line_end) {
        clog(CLOG_WARNING, "Partial request. Ignoring");
        free(buffer);
        return (http_request_t){0};
    }
    size_t line_len = line_end - buffer;
    if (line_len > 0 && buffer[line_len - 1] == '\r') {
        line_len--;  /* strip trailing \r */
    }

    http_request_t request = {0};

    char *method_start = buffer;
    char *method_end = memchr(buffer, ' ', line_len);
    if (!method_end) {
        clog(CLOG_ERROR, "Malformed request line. Ignoring");
        free(buffer);
        return (http_request_t){0};
    }
    size_t method_len = method_end - method_start;

    request.method = http_method_from_str(method_start, method_len);
    if (request.method < 0) {
        clog(CLOG_ERROR, "Invalid HTTP method %.*s", (int)method_len, method_start);
        free(buffer);
        return (http_request_t){0};
    }

    char *path_start = method_end + 1;
    size_t path_remaining = line_len - method_len - 1;
    char *path_end = memchr(path_start, ' ', path_remaining);
    if (!path_end) {
        clog(CLOG_ERROR, "Malformed request. Ignoring");
        free(buffer);
        return (http_request_t){0};
    }
    size_t path_len = path_end - path_start;

    char *version_start = path_end + 1;
    size_t version_len = line_len - (version_start - buffer);

    if (version_len != 8 || memcmp(version_start, "HTTP/1.1", 8) != 0) {
        clog(CLOG_ERROR, "Unsupported HTTP version %.*s", (int)version_len, version_start);
        free(buffer);
        return (http_request_t){0};
    }
    request.version = HTTP_VERSION_1_1;
    request.path = strndup(path_start, path_len);

    char *line;
    for (line = strtok(line_end + 1, "\n"); line; line = strtok(NULL, "\n")) {
        size_t hlen = strlen(line);
        if (hlen > 0 && line[hlen - 1] == '\r') line[hlen - 1] = '\0';

        char *colon = strchr(line, ':');
        if (!colon) continue;

        size_t key_len = colon - line;
        char *value = colon + 1;
        while (*value == ' ') value++;

        if (key_len == 10 && strncasecmp(line, "Connection", 10) == 0) {
            request.connection = (strcasecmp(value, "close") == 0)
                ? HTTP_CONNECTION_CLOSE
                : HTTP_CONNECTION_KEEP_ALIVE;
            clog(CLOG_TRACE, "Parsed Connection header: %s", value);
        }
    }

    free(buffer);

    return request;
}


void http_send_response(int fd, http_response_t response) {
    char header[1024];
    // Always stamped here, rather than relying on each call site to set
    // response.date itself — that's how it ended up defaulting to the epoch
    // on responses built outside fetch_response's GET path (e.g. BAD_REQUEST).
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char date[32];
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    size_t header_len = 0;
    if (response.location) header_len = snprintf(header, 1024, "HTTP/1.1 %i %s\r\nDate: %s\r\nServer: Foxtails\r\nContent-Type: %s\r\nContent-Length: %lu\r\nLocation: %s\r\n\r\n",
                                        response.code, response.reason, date, mime_type_str(response.mime_type), strlen(response.content), response.location);
    else header_len = snprintf(header, 1024, "HTTP/1.1 %i %s\r\nDate: %s\r\nServer: Foxtails\r\nContent-Type: %s\r\nContent-Length: %lu\r\n\r\n",
                                        response.code, response.reason, date, mime_type_str(response.mime_type), strlen(response.content));


    size_t response_len = header_len + strlen(response.content);
    char *resp = malloc(header_len + strlen(response.content)+1);
    if (!resp) {
        clog(CLOG_ERROR, "Failed to allocate memory for response (fd=%d)", fd);
        return;
    }
    memset(resp, 0, response_len);

    strncat(resp, header, header_len);
    strncat(resp, response.content, strlen(response.content));

    ssize_t sent = send(fd, resp, response_len, 0);
    if (sent < 0) {
        clog(CLOG_WARNING, "Failed to send response on fd=%d: %s", fd, strerror(errno));
    } else {
        clog(CLOG_DEBUG, "Sent %i response (%zu bytes) on fd=%d", response.code, response_len, fd);
    }

    free(resp);
}
