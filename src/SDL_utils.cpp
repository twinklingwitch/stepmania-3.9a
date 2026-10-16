#include "global.h"

#include <SDL3/SDL.h>
#include "SDL_utils.h"
#include "SDL_rotozoom.h"	// for setting icon
#include "RageSurface_Load.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageSurface.h"
#include "RageSurfaceUtils.h"
#include "RageThreads.h"
#include "StepMania.h"
#include "ProductInfo.h"
#include "FrameStats.h"

#include <errno.h>

/* SDL sometimes fails to set an error, in which case we get the null string.  We
 * sometimes use that as a sentinel return value.  This function returns "(none)"
 * if no error is set. */
CString mySDL_GetError()
{
	CString error = SDL_GetError();
	if( error == "" )
		return "(none)"; /* SDL sometimes fails to set an error */
	return error;
}

struct PumpTime
{
	Uint64 iStart, iEnd;
};
static vector<PumpTime> g_Pumps;
static unsigned g_iPumpCount = 0;

void mySDL_PumpEvents()
{
	PumpTime p;
	p.iStart = SDL_GetTicksNS();
	SDL_PumpEvents();
	p.iEnd = SDL_GetTicksNS();
	FrameStats::AddPart( FrameStats::PART_PUMP_EVENTS, p.iEnd - p.iStart );
	++g_iPumpCount;

	/* Nobody is reading events if this gets long; don't let it grow. */
	if( g_Pumps.size() >= 16 )
		g_Pumps.erase( g_Pumps.begin() );
	g_Pumps.push_back( p );
}

bool mySDL_StampedWhilePumping( Uint64 iTimestampNS )
{
	for( unsigned i = 0; i < g_Pumps.size(); ++i )
		if( iTimestampNS >= g_Pumps[i].iStart && iTimestampNS <= g_Pumps[i].iEnd )
			return true;
	return false;
}

void mySDL_ForgetPumps()
{
	g_Pumps.clear();
}

unsigned mySDL_GetPumpCount()
{
	return g_iPumpCount;
}

void mySDL_SetWindowIcon( SDL_Window *pWindow, CString sIconFile )
{
	if( sIconFile.empty() )
		return;

	CString error;
	RageSurface *srf = RageSurfaceUtils::LoadFile( sIconFile, error );
	if( srf == NULL )
		return;

	mySDL_SetWindowIcon( pWindow, srf );
}

void mySDL_SetWindowIcon( SDL_Window *pWindow, RageSurface *srf )
{
#if !defined(DARWIN)
	/* Windows icons are 32x32 and SDL can't resize them for us, which
	 * causes mask corruption.  (Actually, the above icon *is* 32x32;
	 * this is here just in case it changes.) */
	RageSurfaceUtils::Zoom( srf, 32, 32 );

	SDL_Surface *sdl_srf = SDLSurfaceFromRageSurface( srf );
	SDL_SetWindowIcon( pWindow, sdl_srf );
	SDL_DestroySurface( sdl_srf );
#endif
	delete srf;
}

/* Create an SDL surface from a RageSurface. */
SDL_Surface *SDLSurfaceFromRageSurface( RageSurface *surf )
{
	const SDL_PixelFormat format = SDL_GetPixelFormatForMasks( surf->fmt.BitsPerPixel,
		surf->fmt.Rmask, surf->fmt.Gmask, surf->fmt.Bmask, surf->fmt.Amask );
	if( format == SDL_PIXELFORMAT_UNKNOWN )
		RageException::Throw( "No SDL pixel format for %i bpp (%08x, %08x, %08x, %08x)", surf->fmt.BitsPerPixel,
			surf->fmt.Rmask, surf->fmt.Gmask, surf->fmt.Bmask, surf->fmt.Amask );

	SDL_Surface *ret = SDL_CreateSurface( surf->w, surf->h, format );
	if( ret == NULL )
		RageException::Throw( "SDL_CreateSurface(%i, %i): %s", surf->w, surf->h, mySDL_GetError().c_str() );

	/* SDL's pitch isn't necessarily ours. */
	const int iRowBytes = min( surf->pitch, ret->pitch );
	for( int y = 0; y < surf->h; ++y )
		memcpy( (uint8_t *) ret->pixels + y*ret->pitch, surf->pixels + y*surf->pitch, iRowBytes );

	/* Copy the palette. */
	if( surf->format->BytesPerPixel == 1 )
	{
		ASSERT( sizeof(RageSurfaceColor) == sizeof(SDL_Color) );

		SDL_Palette *pPalette = SDL_CreateSurfacePalette( ret );
		if( pPalette == NULL || !SDL_SetPaletteColors( pPalette, (SDL_Color *) surf->fmt.palette->colors, 0, 256 ) )
		{
			const CString sError = mySDL_GetError();
			SDL_DestroySurface( ret );
			RageException::Throw( "Couldn't set the SDL palette: %s", sError.c_str() );
		}

		/* If we have one alpha value, transfer it to the color key. */
		int iKey = -1;
		bool bUsesColorKey = true;

		for( int i = 0; i < surf->format->palette->ncolors; ++i )
		{
			if( surf->format->palette->colors[i].a == 0xFF )
				continue;
			if( surf->format->palette->colors[i].a != 0 )
			{
				bUsesColorKey = false;
				break;
			}

			/* a == 0 */
			if( iKey != -1 )
			{
				bUsesColorKey = false;
				break;
			}
			iKey = i;
		}

		if( bUsesColorKey && iKey != -1 )
			SDL_SetSurfaceColorKey( ret, true, iKey );

		/* XXX: If we have more than one alpha value, or any alpha values between 1 and
		 * 254, it needs to be converted to RGBA.  I'm not sure that we ever do that
		 * with surfaces passed to SDL, though. */
	}

	return ret;
}

#if defined(UNIX)
#include "archutils/Unix/EmergencyShutdown.h"
void EmergencyShutdownSDL()
{
	/* Only do this if the main thread crashes; trying to shut down from
	 * another thread causes crashes (eg. GL may be using TLS). */
	if( !strcmp(RageThread::GetCurThreadName(), "Main thread") && SDL_WasInit(SDL_INIT_VIDEO) )
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
#endif

void SetupSDL()
{
	static bool bDone = false;
	if( bDone )
		return;
	bDone = true;

	/* We have our own crash and signal handling; don't let SDL install any. */
	SDL_SetHint( SDL_HINT_NO_SIGNAL_HANDLERS, "1" );

	/* SDL 1.2 delivered joystick events whether or not the window had focus,
	 * and cabinets depend on that. */
	SDL_SetHint( SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1" );

	/* Under X11 this becomes the WM_CLASS and under Wayland the app_id. */
	SDL_SetAppMetadata( PRODUCT_NAME, PRODUCT_VER, "stepmania" );

	/* The default maps the number row of a French keyboard to the symbols
	 * printed on it, which SDL 1.2 didn't do. */
	SDL_SetHint( SDL_HINT_KEYCODE_OPTIONS, "latin_letters" );

	/* Start SDL (with no subsystems), to make sure we don't use SDL's error handler. */
	SDL_Init( 0 );

	/* Clean up on exit. */
	atexit( SDL_Quit );

#if defined(UNIX)
	/* Attempt to shut down the window on crash. */
        RegisterEmergencyShutdownCallback( EmergencyShutdownSDL );
#endif
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
