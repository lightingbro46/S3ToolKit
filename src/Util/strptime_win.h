#ifndef ZLMEDIAKIT_STRPTIME_WIN_H
#define ZLMEDIAKIT_STRPTIME_WIN_H

#include <ctime>
#ifdef _WIN32
//Implement strptime function on Windows, Linux already provides strptime
//Implement strptime function on Windows platform
char * strptime(const char *buf, const char *fmt, struct tm *tm);
#endif
#endif //ZLMEDIAKIT_STRPTIME_WIN_H
