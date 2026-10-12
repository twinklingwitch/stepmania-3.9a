#ifndef WINDOWS_ARCH_H
#define WINDOWS_ARCH_H

/* Everything else comes from arch_default.h: SDL for the window, input, sound and
 * error dialogs, and null drivers for memory cards and lights. */
#include "LoadingWindow/LoadingWindow_SDL.h"

#include "ArchHooks/ArchHooks_Win32.h"

#include "Lights/LightsDriver_Win32Parallel.h"

#include "MemoryCard/MemoryCardDriverThreaded_Windows.h"

#endif
