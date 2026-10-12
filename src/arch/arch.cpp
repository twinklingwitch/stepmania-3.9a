/*
 * This file provides functions to create driver objects.
 */

#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"

#include "PrefsManager.h"
#include "arch.h"
#include "arch_platform.h"

LoadingWindow *MakeLoadingWindow()
{
	if( !PREFSMAN->m_bShowLoadingWindow )
		return new LoadingWindow_Null;

	/* Don't load NULL by default.  On most systems, if we can't load the SDL
	 * loading window, we won't be able to init OpenGL, either, so don't bother. */
	CString drivers = "sdl";
	CStringArray DriversToTry;
	split(drivers, ",", DriversToTry, true);

	ASSERT( DriversToTry.size() != 0 );

	CString Driver;
	LoadingWindow *ret = NULL;

	for(unsigned i = 0; ret == NULL && i < DriversToTry.size(); ++i)
	{
		try {
			Driver = DriversToTry[i];

#ifdef HAVE_LOADING_WINDOW_SDL
			if(!DriversToTry[i].CompareNoCase("SDL")) ret = new LoadingWindow_SDL;
#endif
#ifdef HAVE_LOADING_WINDOW_NULL
			if(!DriversToTry[i].CompareNoCase("Null")) ret = new LoadingWindow_Null;
#endif
		} catch(const RageException &e) {
			LOG->Info("Couldn't load driver %s: %s", DriversToTry[i].c_str(), e.what());
		}
	}
	
	if(ret)
		LOG->Info("Loading window: %s", Driver.c_str());
	
	return ret;
}

LowLevelWindow *MakeLowLevelWindow() { return new ARCH_LOW_LEVEL_WINDOW; }

void MakeInputHandlers(vector<InputHandler *> &Add)
{
	Add.push_back(new InputHandler_SDL);
}

RageSoundDriver *MakeRageSoundDriver(CString drivers)
{
	CStringArray DriversToTry;
	split(drivers, ",", DriversToTry, true);

	ASSERT( DriversToTry.size() != 0 );

	CString Driver;
	RageSoundDriver *ret = NULL;

	for(unsigned i = 0; ret == NULL && i < DriversToTry.size(); ++i)
	{
		try {
			Driver = DriversToTry[i];
			LOG->Trace("Initializing driver: %s", DriversToTry[i].c_str());

#ifdef HAVE_ALSA
			if(!DriversToTry[i].CompareNoCase("ALSA")) ret = new RageSound_ALSA9;
			if(!DriversToTry[i].CompareNoCase("ALSA-sw")) ret = new RageSound_ALSA9_Software;
#endif		
#ifdef HAVE_OSS
			if(!DriversToTry[i].CompareNoCase("OSS")) ret = new RageSound_OSS;
#endif
#ifdef HAVE_SNDIO
			if(!DriversToTry[i].CompareNoCase("sndio")) ret = new RageSound_Sndio;
#endif
			if(!DriversToTry[i].CompareNoCase("SDL")) ret = new RageSound_SDL;
			if(!DriversToTry[i].CompareNoCase("Null")) ret = new RageSound_Null;
			if( !ret )
				LOG->Warn("Unknown sound driver name: %s", DriversToTry[i].c_str());
		} catch(const RageException &e) {
			LOG->Info("Couldn't load driver %s: %s", DriversToTry[i].c_str(), e.what());
		}
	}
	
	if(ret)
		LOG->Info("Sound driver: %s", Driver.c_str());
	
	return ret;
}

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
