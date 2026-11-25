#include <signal.h>
#include <stdbool.h>
#include <config.h>

#define CLOG_IMPLEMENTATION
#include <clog.h>


bool running = true;

void sigint(int _) {(void)_;running = false;}

int main(void) {
    signal(SIGINT, sigint);

    clog_set_fmt("[Foxfang] " CLOG_PRETTY_FMT);
    clog(CLOG_INFO, "Startup");

    while (running);

    clog(CLOG_INFO, "Shutdown");
    return 0;
}
