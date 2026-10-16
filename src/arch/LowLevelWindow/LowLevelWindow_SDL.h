#ifndef LOW_LEVEL_WINDOW_SDL_H
#define LOW_LEVEL_WINDOW_SDL_H

#include "LowLevelWindow.h"

struct SDL_Window;
struct SDL_GLContextState;

class LowLevelWindow_SDL: public LowLevelWindow
{
	RageDisplay::VideoModeParams CurrentParams;

	SDL_Window *m_pWindow;
	SDL_GLContextState *m_pContext;	// SDL_GLContext

	/* The GL context can't change its pixel format, so we need a new window
	 * if this changes. */
	int m_iWindowBpp;

	/* The last size we asked for while the window wasn't fullscreen.  That's
	 * the size SDL takes it back to when it leaves fullscreen. */
	int m_iWindowedWidth, m_iWindowedHeight;

	/* The size of the framebuffer in pixels.  It's bigger than the window on
	 * high density displays. */
	int m_iFramebufferWidth, m_iFramebufferHeight;

	bool m_bCoreProfile;

	void DestroyWindow();

public:
	LowLevelWindow_SDL();
	~LowLevelWindow_SDL();
	void *GetProcAddress(CString s);
	void SetCoreProfile( bool b ) { m_bCoreProfile = b; }
	CString TryVideoMode( RageDisplay::VideoModeParams p, bool &bNewDeviceOut );
	void SwapBuffers();
	float GetRefreshRate() const;
	void HideWindow();
	void Update(float fDeltaTime);

	RageDisplay::VideoModeParams GetVideoModeParams() const { return CurrentParams; }
	void GetFramebufferSize( int &iWidth, int &iHeight ) const { iWidth = m_iFramebufferWidth; iHeight = m_iFramebufferHeight; }
};
#undef ARCH_LOW_LEVEL_WINDOW
#define ARCH_LOW_LEVEL_WINDOW LowLevelWindow_SDL

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
