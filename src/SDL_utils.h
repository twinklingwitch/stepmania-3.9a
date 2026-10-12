/* Various small SDL tools. */

#ifndef SM_SDL_UTILS
#define SM_SDL_UTILS

#include <SDL3/SDL.h>
struct RageSurface;

CString mySDL_GetError();

/* SDL gives events a timestamp, but most backends set it to the time SDL read the
 * event, not the time it happened.  Use mySDL_PumpEvents instead of SDL_PumpEvents
 * to be able to tell those apart: an event stamped while we were pumping has no
 * information about when it really happened (the first Wayland event, and the ones
 * that arrive faster than any before them, are stamped that way too).  Call
 * mySDL_ForgetPumps once the events queued so far have been read. */
void mySDL_PumpEvents();
bool mySDL_StampedWhilePumping( Uint64 iTimestampNS );
void mySDL_ForgetPumps();

/* Set the window icon from an image file.  The surface variant takes
 * ownership of the surface. */
void mySDL_SetWindowIcon( SDL_Window *pWindow, CString sIconFile );
void mySDL_SetWindowIcon( SDL_Window *pWindow, RageSurface *pIcon );
SDL_Surface *SDLSurfaceFromRageSurface( RageSurface *surf );

void SetupSDL();

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
