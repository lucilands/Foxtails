#include <routing.h>
#include <http.h>
#include <clog.h>
#include <cpool.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>


static bool append_route(route_table_t *routes, route_t route) {
    route_t *new_routes = realloc(routes->routes, (routes->len + 1) * sizeof(route_t));
    if (!new_routes) {
        clog(CLOG_ERROR, "Failed to allocate memory for route");
        return false;
    }
    new_routes[routes->len] = route;
    routes->routes = new_routes;
    routes->len++;
    return true;
}

static int route_str_to_type(const char *str) {
    if (strcasecmp(str, "alias") == 0) return ROUTE_TYPE_ALIAS;
    if (strcasecmp(str, "reroute") == 0) return ROUTE_TYPE_REROUTE;
    return -1;
}

static int route_str_to_methods(const char *str) {
    if (strcmp(str, "*") == 0 || strcasecmp(str, "ALL") == 0) return REQUEST_ALL;

    char *copy = pstrdup((char*)str);
    int methods = 0;
    for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        int m = http_method_from_str(tok, strlen(tok));
        if (m < 0) {
            return -1;
        }
        methods |= m;
    }
    return methods;
}

route_table_t routes_parse(const char *restrict path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0)  {
        clog(CLOG_WARNING, "Failed to open config file %s", path);
        return (route_table_t) {};
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        clog(CLOG_WARNING, "Failed to get file size of file %s", path);
        close(fd);
        return (route_table_t) {};
    }

    if (sb.st_size == 0) {
        clog(CLOG_WARNING, "Config file %s is empty", path);
        close(fd);
        return (route_table_t) {};
    }

    char *map = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        clog(CLOG_WARNING, "Failed to map file");
        close(fd);
        return (route_table_t) {};
    } 

    route_table_t ret = {0};

    char *line_start = (char*)map;
    char *line_end = line_start;
    char *line = NULL;
    size_t line_no = 0;
    while ((line_end = memchr(line_start, '\n', sb.st_size - (line_start - map)))) {
        line_no++;
        int line_len = line_end - line_start;
        
        if (line_len == 0 || line_start[0] == '#'){
            line_start = line_end+1;
            continue;
        }

        char *cur_line_start = line_start;
        line_start = line_end+1;

        cpool_save();
        line = pstrndup(cur_line_start, line_len);

        char *methods_str = strtok(line, " \t\v");
        if (!methods_str) goto malformed_line;

        char *route = strtok(NULL, " \t\v");
        if (!route) goto malformed_line;

        char *type_str = strtok(NULL, " \t\v");
        if (!type_str) goto malformed_line;

        char *target = strtok(NULL, " \t\v");
        if (!target) goto malformed_line;

        int type = route_str_to_type(type_str);
        if (type < 0) {
            clog(CLOG_ERROR, "Unknown route type '%s' at %s:%zu", type_str, path, line_no);
            goto malformed_line;
        }

        int methods = route_str_to_methods(methods_str);
        if (methods < 0) {
            clog(CLOG_ERROR, "Unknown method in '%s' at %s:%zu", methods_str, path, line_no);
            goto malformed_line;
        }

        {
            size_t route_off = route - line, route_len = strlen(route);
            size_t target_off = target - line, target_len = strlen(target);
            cpool_restore();

            if (!append_route(&ret, (route_t) {
                .type = type,
                .methods = methods,
                .dest = pstrndup(cur_line_start + target_off, target_len),
                .src = pstrndup(cur_line_start + route_off, route_len)
            })) {
                clog(CLOG_ERROR, "Failed to store route at %s:%zu", path, line_no);
            }
        }

        goto end;
malformed_line:
        clog(CLOG_ERROR, "Malformed route %s:%zu", path, line_no);
        cpool_restore();
end:
        ;
    }

    munmap(map, sb.st_size);
    close(fd);

    clog(CLOG_INFO, "Loaded %u route(s) from %s", ret.len, path);
    return ret;
}

void routes_delete(route_table_t routes) {
    free(routes.routes);
}
