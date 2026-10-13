#include "global.h"

#include "SDL_utils.h"
#include "RageSurface_Load_XPM.h"
#include "LoadingWindow_SDL.h"
#include "loading.xpm"
#include "StepMania.xpm" /* icon */
#include "RageSurface.h"
#include "RageSurfaceUtils.h"

LoadingWindow_SDL::LoadingWindow_SDL()
{
	window = NULL;

	/* Initialize the SDL library */
	if( !SDL_InitSubSystem(SDL_INIT_VIDEO) )
		RageException::Throw( "Couldn't initialize SDL: %s", SDL_GetError() );

	/* On X11, SDL would otherwise put the window surface in an OpenGL texture,
	 * which means loading the GL driver (only to unload it and load it again for
	 * the real window) and drawing through it every time we're painted.  The
	 * plain X11 framebuffer is all a still picture needs.  The environment can
	 * still override this. */
	const char *szVideoDriver = SDL_GetCurrentVideoDriver();
	if( szVideoDriver != NULL && !strcmp(szVideoDriver, "x11") )
		SDL_SetHint( SDL_HINT_FRAMEBUFFER_ACCELERATION, "0" );

	/* Load the BMP - we need its dimensions */
	CString error;
	RageSurface *srf = RageSurface_Load_XPM( loading, error );
	if( srf == NULL )
	{
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		RageException::Throw( "Couldn't load loading.bmp: %s", error.c_str() );
	}

	/* The destructor doesn't run if we throw, so clean up here. */
	try
	{
		/* Initialize the window */
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetStringProperty( props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Loading StepMania" );
		SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED );
		SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED );
		SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, srf->w );
		SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, srf->h );
		SDL_SetBooleanProperty( props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true );
		window = SDL_CreateWindowWithProperties( props );
		const CString sCreateError = mySDL_GetError();
		SDL_DestroyProperties( props );
		if( window == NULL )
			RageException::Throw( "Couldn't initialize loading window: %s", sCreateError.c_str() );

		/* Set window icon */
		RageSurface *icon_srf = RageSurface_Load_XPM( icon, error );

		uint32_t color;
		if( icon_srf->fmt.MapRGBA( 0xFF, 0, 0xFF, 0xFF, color ) )
			icon_srf->format->palette->colors[ color ].a = 0;

		mySDL_SetWindowIcon( window, icon_srf );

		SDL_Surface *pSDLSurface = SDLSurfaceFromRageSurface( srf );
		SDL_Surface *pWindowSurface = SDL_GetWindowSurface( window );
		const bool bBlitted = pWindowSurface != NULL && SDL_BlitSurface( pSDLSurface, NULL, pWindowSurface, NULL );
		const CString sBlitError = mySDL_GetError();
		SDL_DestroySurface( pSDLSurface );
		if( !bBlitted )
			RageException::Throw( "Couldn't draw the loading window: %s", sBlitError.c_str() );
	}
	catch(...)
	{
		delete srf;
		if( window != NULL )
			SDL_DestroyWindow( window );
		window = NULL;
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		throw;
	}

	delete srf;

	SDL_UpdateWindowSurface( window );
	mySDL_PumpEvents();
}

LoadingWindow_SDL::~LoadingWindow_SDL()
{
	SDL_DestroyWindow( window );
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	SDL_ResetHint( SDL_HINT_FRAMEBUFFER_ACCELERATION );
}

void LoadingWindow_SDL::Paint()
{
	/* This is called for every song and course loaded, but the picture never
	 * changes, so a few times a second is plenty. */
	if( m_LastPaint.Ago() < 1/30.0f )
		return;
	m_LastPaint.Touch();

	SDL_UpdateWindowSurface( window );

	/* Keep the window manager from thinking we've hung. */
	mySDL_PumpEvents();
}

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
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
