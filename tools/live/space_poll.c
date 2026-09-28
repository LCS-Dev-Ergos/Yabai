// Prints every change of the active Space with CLOCK_UPTIME_RAW time in ms.
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern int SLSMainConnectionID(void);
extern uint64_t SLSGetActiveSpace(int cid);

int main(int argc, char **argv)
{
    double seconds = argc > 1 ? atof(argv[1]) : 5.0;
    int cid = SLSMainConnectionID();
    uint64_t last = 0;
    uint64_t start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);

    for (;;) {
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (now - start > seconds * 1e9) break;

        uint64_t sid = SLSGetActiveSpace(cid);
        if (sid != last) {
            printf("%.1f %llu\n", now / 1e6, sid);
            fflush(stdout);
            last = sid;
        }

        usleep(1000);
    }

    return 0;
}
