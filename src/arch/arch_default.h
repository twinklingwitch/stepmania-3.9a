#ifndef SDL_ARCH_H
#define SDL_ARCH_H

/* All driver types should have a default, portable implementation, provided
 * here.
 *
 * The basic design principle of arch is that a new platform can be up and running
 * quickly using only arch_default, with OpenGL graphics and SDL input, and things can
 * be overloaded later.  That way, people don't have to learn how arch works
 * until after they have the basic stuff running. */

#include "InputHandler/InputHandler_SDL.h"

/* Load default fallback drivers; some of these may be overridden by arch-specific
 * drivers.  These are all singleton drivers--we never use more than one. */
#include "LoadingWindow/LoadingWindow_Null.h"
#include "Sound/RageSoundDriver_Null.h"
#include "Sound/RageSoundDriver_SDL.h"
#include "Lights/LightsDriver_Null.h"
#include "Lights/LightsDriver_SystemMessage.h"
#include "MemoryCard/MemoryCardDriver_Null.h"

#include "LowLevelWindow/LowLevelWindow_SDL.h"

#endif

/*
 * (c) 2002-2004 Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
