#ifndef __ROUTING_H
#define __ROUTING_H

enum {
    ROUTE_TYPE_ALIAS = 0,
    ROUTE_TYPE_REROUTE,
};

typedef struct {
    char *src;
    char *dest;

    int type;
    int methods; // Bitmask of REQUEST_* from http.h (see REQUEST_ALL for "any method").
} route_t;

typedef struct {
    route_t *routes;
    unsigned int len;
} route_table_t;

route_table_t routes_parse(const char *restrict path);
void routes_delete(route_table_t routes);

#endif //__ROUTING_H
