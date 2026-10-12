#include "global.h"
#include "LowLevelWindow_SDL.h"
#include "SDL_utils.h"
#include "RageLog.h"
#include "RageDisplay.h" // for REFRESH_DEFAULT
#include "PrefsManager.h"
#include "StepMania.h"

#if defined(DARWIN)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <string.h>

LowLevelWindow_SDL::LowLevelWindow_SDL()
{
	m_pWindow = NULL;
	m_pContext = NULL;
	m_iWindowBpp = 0;
	m_iWindowedWidth = m_iWindowedHeight = 0;
	m_iFramebufferWidth = m_iFramebufferHeight = 0;

	if( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
		RageException::Throw( "SDL_INIT_VIDEO failed: %s", mySDL_GetError().c_str() );

	const int iVersion = SDL_GetVersion();
	LOG->Info( "SDL version: %i.%i.%i", SDL_VERSIONNUM_MAJOR(iVersion), SDL_VERSIONNUM_MINOR(iVersion), SDL_VERSIONNUM_MICRO(iVersion) );
	LOG->Info( "SDL video driver: %s", SDL_GetCurrentVideoDriver() );

	/* By default, ignore the input events.  InputHandler_SDL enables them as it
	 * needs them; if nobody is reading them, they'd just fill up the event queue. */
	for( Uint32 i = SDL_EVENT_KEY_DOWN; i <= SDL_EVENT_KEY_UP; ++i )
		SDL_SetEventEnabled( i, false );
	for( Uint32 i = SDL_EVENT_JOYSTICK_AXIS_MOTION; i <= SDL_EVENT_JOYSTICK_REMOVED; ++i )
		SDL_SetEventEnabled( i, false );
}

LowLevelWindow_SDL::~LowLevelWindow_SDL()
{
	DestroyWindow();
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

void LowLevelWindow_SDL::DestroyWindow()
{
	if( m_pContext )
		SDL_GL_DestroyContext( m_pContext );
	m_pContext = NULL;

	/* Destroying the window with keys held down makes SDL queue the key-ups
	 * for them. */
	if( m_pWindow )
		SDL_DestroyWindow( m_pWindow );
	m_pWindow = NULL;
}

void *LowLevelWindow_SDL::GetProcAddress(CString s)
{
	return (void *) SDL_GL_GetProcAddress( s.c_str() );
}

/* Window operations can complete asynchronously; wait for them, so the window
 * is in its final state when we return.  This gives up after a short time. */
static void SyncWindow( SDL_Window *pWindow )
{
	if( !SDL_SyncWindow(pWindow) )
		LOG->Trace( "SDL_SyncWindow: the window didn't reach its requested state in time" );
}

/* Put the window in fullscreen at the requested mode.  Returns "" on success. */
static CString SetFullscreen( SDL_Window *pWindow, const RageDisplay::VideoModeParams &p )
{
	SDL_DisplayMode mode;
	const float fRefresh = (p.rate == REFRESH_DEFAULT)? 0:p.rate;
	const SDL_DisplayMode *pMode = NULL;
	if( SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(pWindow), p.width, p.height, fRefresh, false, &mode) )
		pMode = &mode;
	else
		LOG->Info( "No fullscreen mode for %ix%i (%s); using the desktop mode", p.width, p.height, mySDL_GetError().c_str() );

	if( !SDL_SetWindowFullscreenMode(pWindow, pMode) || !SDL_SetWindowFullscreen(pWindow, true) )
		return mySDL_GetError();

	return "";
}

CString LowLevelWindow_SDL::TryVideoMode( RageDisplay::VideoModeParams p, bool &bNewDeviceOut )
{
	ASSERT( p.bpp == 16 || p.bpp == 32 );

	bNewDeviceOut = false;

	if( m_pWindow == NULL || p.bpp != m_iWindowBpp )
	{
		DestroyWindow();

		switch( p.bpp )
		{
		case 16:
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
			break;
		case 32:
			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		}

		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, true);

#if defined(DARWIN)
		/* We draw with the fixed function pipeline, which macOS only has in its
		 * legacy (2.1) contexts.  That's what SDL gives by default, but be sure. */
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

		/* On Retina displays (and scaled Wayland outputs) draw at the display's
		 * resolution rather than letting it scale up a low resolution picture.
		 * Window sizes stay in the usual units; only the framebuffer is bigger. */
		SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if( !p.windowed )
			flags |= SDL_WINDOW_FULLSCREEN;

		m_pWindow = SDL_CreateWindow( p.sWindowTitle, p.width, p.height, flags );
		if( m_pWindow == NULL )
		{
			LOG->Trace( "SDL_CreateWindow failed: %s", mySDL_GetError().c_str() );
			return mySDL_GetError();	// failed to set mode
		}

		m_pContext = SDL_GL_CreateContext( m_pWindow );
		if( m_pContext == NULL )
		{
			const CString err = mySDL_GetError();
			LOG->Trace( "SDL_GL_CreateContext failed: %s", err.c_str() );
			DestroyWindow();
			return err;
		}

		m_iWindowBpp = p.bpp;
		m_iWindowedWidth = p.width;
		m_iWindowedHeight = p.height;
		mySDL_SetWindowIcon( m_pWindow, p.sIconFile );

		bNewDeviceOut = true;
	}
	else
	{
		/* Keep the window and its GL context, so textures stay valid. */
		SDL_SetWindowTitle( m_pWindow, p.sWindowTitle );

		/* SDL ignores size requests while the window is fullscreen, so leave
		 * fullscreen first when we want a window.  Only ask for a size if it
		 * isn't the one the window is going back to anyway: a tiling compositor
		 * picks its own size, and requesting ours would fight it. */
		const bool bFullscreen = (SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
		if( bFullscreen && p.windowed )
		{
			SDL_SetWindowFullscreen( m_pWindow, false );
			SyncWindow( m_pWindow );
		}

		if( (!bFullscreen || p.windowed) && (p.width != m_iWindowedWidth || p.height != m_iWindowedHeight) )
		{
			if( !SDL_SetWindowSize(m_pWindow, p.width, p.height) )
				LOG->Info( "SDL_SetWindowSize failed: %s", mySDL_GetError().c_str() );
			SyncWindow( m_pWindow );
			m_iWindowedWidth = p.width;
			m_iWindowedHeight = p.height;
		}
	}

	if( !p.windowed )
	{
		const CString err = SetFullscreen( m_pWindow, p );
		if( err != "" )
		{
			LOG->Trace( "Setting fullscreen mode failed: %s", err.c_str() );
			if( bNewDeviceOut )
			{
				/* Don't leave a window around that nobody knows is new. */
				DestroyWindow();
				bNewDeviceOut = false;
			}
			return err;
		}
	}

	SyncWindow( m_pWindow );

	/* This applies to the current context, so it has to be set every time. */
	if( !SDL_GL_SetSwapInterval(p.vsync? 1:0) )
		LOG->Info( "SDL_GL_SetSwapInterval failed: %s", mySDL_GetError().c_str() );

	if( p.windowed )
		SDL_ShowCursor();
	else
		SDL_HideCursor();

	/* Report the size we asked for, since this ends up in the preferences.  If
	 * the window manager gives us something else (eg. a tiling compositor),
	 * the resize event in Update() takes care of it. */
	CurrentParams = p;

	/* The framebuffer size is needed right away, for the viewport. */
	if( !SDL_GetWindowSizeInPixels(m_pWindow, &m_iFramebufferWidth, &m_iFramebufferHeight) ||
		m_iFramebufferWidth <= 0 || m_iFramebufferHeight <= 0 )
	{
		m_iFramebufferWidth = p.width;
		m_iFramebufferHeight = p.height;
	}
	if( m_iFramebufferWidth != p.width || m_iFramebufferHeight != p.height )
		LOG->Info( "Framebuffer: %ix%i pixels", m_iFramebufferWidth, m_iFramebufferHeight );

	{
		/* Find out what we really got. */
		int r,g,b,a, colorbits, depth, stencil;
		
		SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &r);
		SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &g);
		SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &b);
		SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &a);
		SDL_GL_GetAttribute(SDL_GL_BUFFER_SIZE, &colorbits);
		SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
		SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &stencil);
		LOG->Info("Got %i bpp (%i%i%i%i), %i depth, %i stencil",
			colorbits, r, g, b, a, depth, stencil);
	}

	return "";	// we set the video mode successfully
}

void LowLevelWindow_SDL::SwapBuffers()
{
	SDL_GL_SwapWindow( m_pWindow );

	/* On X11 the driver lets us queue up frames ahead of the display, so what's
	 * shown is older than it needs to be.  Wait for the swap to finish.  Wayland
	 * already throttles us to the compositor, so don't bother there. */
	if( PREFSMAN->m_bFastRendering && !strcmp(SDL_GetCurrentVideoDriver(), "x11") )
		glFinish();
}

float LowLevelWindow_SDL::GetRefreshRate() const
{
	if( m_pWindow == NULL )
		return 0;

	const SDL_DisplayMode *pMode = NULL;
	if( SDL_GetWindowFlags(m_pWindow) & SDL_WINDOW_FULLSCREEN )
		pMode = SDL_GetWindowFullscreenMode( m_pWindow );
	if( pMode == NULL )
		pMode = SDL_GetCurrentDisplayMode( SDL_GetDisplayForWindow(m_pWindow) );
	return pMode? pMode->refresh_rate: 0;
}

void LowLevelWindow_SDL::Update(float fDeltaTime)
{
	/* This needs to be called before anything that handles SDL events. */
	mySDL_PumpEvents();

	bool bFocusChanged = false;
	bool bQuit = false;

	SDL_Event event;
	while( SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST) > 0 )
	{
		switch( event.type )
		{
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
		case SDL_EVENT_WINDOW_FOCUS_LOST:
		case SDL_EVENT_WINDOW_MINIMIZED:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_HIDDEN:
			bFocusChanged = true;
			break;
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			/* SDL only turns this into SDL_EVENT_QUIT for the last window. */
			if( event.window.windowID == SDL_GetWindowID(m_pWindow) )
				bQuit = true;
			break;
		}
	}

	/* Check the size directly rather than waiting for a resize event: we may
	 * have been given a different size than we asked for without one (eg. the
	 * closest fullscreen mode, or a tiling compositor's size).  The window size
	 * is what goes in the preferences; the framebuffer size is what we draw to.
	 * They differ on high density displays, and the framebuffer can change on
	 * its own when the window moves to another display. */
	int iWidth, iHeight, iPixelWidth, iPixelHeight;
	if( SDL_GetWindowSize(m_pWindow, &iWidth, &iHeight) &&
		SDL_GetWindowSizeInPixels(m_pWindow, &iPixelWidth, &iPixelHeight) &&
		iPixelWidth > 0 && iPixelHeight > 0 &&
		(iWidth != CurrentParams.width || iHeight != CurrentParams.height ||
		 iPixelWidth != m_iFramebufferWidth || iPixelHeight != m_iFramebufferHeight) )
	{
		CurrentParams.width = iWidth;
		CurrentParams.height = iHeight;
		m_iFramebufferWidth = iPixelWidth;
		m_iFramebufferHeight = iPixelHeight;

		/* Let DISPLAY know that our resolution has changed. */
		DISPLAY->ResolutionChanged();
	}

	if( bFocusChanged )
	{
		/* We only count as having focus if we can actually be seen. */
		const SDL_WindowFlags flags = SDL_GetWindowFlags( m_pWindow );
		FocusChanged( (flags & SDL_WINDOW_INPUT_FOCUS) && !(flags & (SDL_WINDOW_MINIMIZED|SDL_WINDOW_HIDDEN)) );
	}

	while( SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_QUIT, SDL_EVENT_QUIT) > 0 )
		bQuit = true;

	if( bQuit )
	{
		LOG->Trace("SDL_EVENT_QUIT: shutting down");
		ExitGame();
	}

	/* Everything else is of no interest to us.  Throw it away, so things like
	 * mouse motion don't pile up; the input handler gets the keyboard and joystick. */
	SDL_FlushEvents( SDL_EVENT_FIRST, SDL_EVENT_KEY_DOWN-1 );
	SDL_FlushEvents( SDL_EVENT_KEY_UP+1, SDL_EVENT_JOYSTICK_AXIS_MOTION-1 );
	SDL_FlushEvents( SDL_EVENT_JOYSTICK_REMOVED+1, SDL_EVENT_LAST );

	if( PREFSMAN->m_bDisableScreenSaver != !SDL_ScreenSaverEnabled() )
	{
		if( PREFSMAN->m_bDisableScreenSaver )
			SDL_DisableScreenSaver();
		else
			SDL_EnableScreenSaver();
	}
}

/*
 * (c) 2003-2004 Chris Danford, Glenn Maynard
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
