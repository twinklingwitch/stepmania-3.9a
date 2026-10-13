#ifndef ARCH_SETUP_WIN32_H
#define ARCH_SETUP_WIN32_H

#include <stdint.h>

/* mingw-w64 ships unistd.h, strings.h and the rest of the POSIX bits we use.  Pull
 * unistd.h in before redefining usleep() below. */
#include <unistd.h>

/* Sleep() and mingw's usleep() only wake up on the system timer tick, 15.6ms by
 * default, which is far too coarse for the frame limiter and the sound threads.
 * ArchUSleep (ArchHooks_Win32.cpp) uses a high resolution timer where there is one. */
void ArchUSleep( unsigned long iMicroseconds );
#define usleep(us) ArchUSleep(us)

#endif
