#include "stdio.h"
#include "string.h"
#include "vfs.h"
#include "path.h"
#include "rtc.h"

int app_date_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    rtc_time_t t;
    rtc_read_time(&t);

    const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    /* Simple day-of-week calculation (Zeller's congruence simplified) */
    int m = (int)t.month;
    int y = (int)t.year;
    if (m < 3) { m += 12; y--; }
    int dow = (y + y/4 - y/100 + y/400 + (13*(m+1))/5 + (int)t.day - 1) % 7;
    if (dow < 0) dow += 7;

    printf("%s %s %02u %02u:%02u:%02u %04u\n",
           days[dow], months[(t.month > 0 && t.month <= 12) ? t.month - 1 : 0],
           t.day, t.hour, t.minute, t.second, t.year);
    return 0;
}
