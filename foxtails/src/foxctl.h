#ifndef __FOXCTL_H
#define __FOXCTL_H

enum {
    FOXCTL_NONE = 0,
    FOXCTL_STOP,
    FOXCTL_RELOAD
};

void foxctl_start(const char *restrict path);
int foxctl_fetch_cmd(int timeout);
void foxctl_stop();

#endif //__FOXCTL_H
