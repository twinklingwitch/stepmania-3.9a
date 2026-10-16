#ifndef LOW_LEVEL_WINDOW_H
#define LOW_LEVEL_WINDOW_H

/* This handles low-level operations that OGL 1.x doesn't give us.  Normally,
 * we use SDL.  Note that not all SDL operations go here; however, nothing
 * outside of this can assume that SDL has VIDEO initialized. */

#include "RageDisplay.h" // for RageDisplay::VideoModeParams

class LowLevelWindow
{
public:
	virtual ~LowLevelWindow() { }

	virtual void *GetProcAddress(CString s) = 0;

	/* Ask for an OpenGL 3.3 core profile context (4.1 on macOS) instead of a
	 * compatibility one, for windows created after this. */
	virtual void SetCoreProfile( bool b ) { }

	// Return "" if mode change was successful, otherwise an error message.
	// bNewDeviceOut is set true if a new device was created and textures
	// need to be reloaded.
	virtual CString TryVideoMode( RageDisplay::VideoModeParams p, bool &bNewDeviceOut ) = 0;

	virtual void SwapBuffers() = 0;

	// The refresh rate the window is displayed at, or 0 if we don't know.
	virtual float GetRefreshRate() const { return 0; }
	virtual void Update(float fDeltaTime) { }
	virtual void HideWindow() { }

	virtual RageDisplay::VideoModeParams GetVideoModeParams() const = 0;

	// The size of the framebuffer in pixels.  This is the window size, except on
	// high density displays (Retina, scaled Wayland outputs), where it's larger.
	virtual void GetFramebufferSize( int &iWidth, int &iHeight ) const
	{
		iWidth = GetVideoModeParams().width;
		iHeight = GetVideoModeParams().height;
	}
};

#endif

/*
 * (c) 2003-2004 Glenn Maynard
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
