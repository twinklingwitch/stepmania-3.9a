#ifndef ARCH_DARWIN_H
#define ARCH_DARWIN_H

/* macOS is a Unix as far as the rest of the arch code is concerned.  SDL does the
 * window, input, sound (CoreAudio), error dialogs and the loading window; memory
 * cards and lights get the null drivers from arch_default.h. */
#include "LoadingWindow/LoadingWindow_SDL.h"

#include "ArchHooks/ArchHooks_Unix.h"

#endif
