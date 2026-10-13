#ifndef ARCH_H
#define ARCH_H

/* Include this file if you need to create an instance of a driver object.  */
class LoadingWindow;
class RageSoundDriver;
class ArchHooks;
class InputHandler;
class LowLevelWindow;
class LightsDriver;
class MemoryCardDriver;

LoadingWindow *MakeLoadingWindow();
LowLevelWindow *MakeLowLevelWindow();

void MakeInputHandlers(vector<InputHandler *> &Add);
RageSoundDriver *MakeRageSoundDriver(CString drivers);

/* These definitions are in here, instead of in arch_*.h, because they
 * need to be available to other modules.  It'd be overkill to create separate
 * "config" and "driver" headers for each arch. */

/* Define the default list of sound drivers.  SDL does the sound everywhere;
 * Null is the fallback if no audio device can be opened. */
#define DEFAULT_SOUND_DRIVER_LIST "SDL,Null"

#define DEFAULT_MOVIE_DRIVER_LIST "FFMpeg,Null"

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
