#include "global.h"
#include "FrameStats.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "RageDisplay.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

static bool IsEnabled()
{
	const char *sz = getenv( "SM_FRAME_STATS" );
	return sz != NULL && *sz != '\0' && strcmp( sz, "0" );
}

bool FrameStats::g_bEnabled = IsEnabled();

namespace
{
	const char *const PhaseNames[FrameStats::NUM_PHASES] =
	{
		"pace",
		"window",
		"sound",
		"screens",
		"input",
		"other",
		"draw",
		"render",
		"present",
		"limit sleep",
		"limit spin",
		"unfocused",
	};

	/* In performance counter ticks; parts are in nanoseconds, since that's
	 * what SDL's event timestamps are. */
	Uint64 g_iLastMark = 0;
	Uint64 g_iFrame[FrameStats::NUM_PHASES];
	Uint64 g_iSum[FrameStats::NUM_PHASES];
	Uint64 g_iWorst[FrameStats::NUM_PHASES];
	Uint64 g_iWorstTotal = 0;

	unsigned long long g_iFramePart[FrameStats::NUM_PARTS];
	unsigned long long g_iSumPart[FrameStats::NUM_PARTS];
	unsigned long long g_iWorstPart[FrameStats::NUM_PARTS];
	int g_iPartCalls[FrameStats::NUM_PARTS];

	int g_iFrames = 0, g_iShown = 0;
	Uint64 g_iPeriodStart = 0;
}

void FrameStats::DoMark( Phase p )
{
	const Uint64 iNow = SDL_GetPerformanceCounter();
	if( g_iLastMark != 0 )
		g_iFrame[p] += iNow - g_iLastMark;
	g_iLastMark = iNow;
}

void FrameStats::DoAddPart( Part p, unsigned long long iNanoseconds )
{
	g_iFramePart[p] += iNanoseconds;
	++g_iPartCalls[p];
}

void FrameStats::DoFrameShown()
{
	++g_iShown;
}

void FrameStats::DoEndFrame()
{
	const Uint64 iNow = SDL_GetPerformanceCounter();
	if( g_iPeriodStart == 0 )
		g_iPeriodStart = iNow;

	Uint64 iTotal = 0;
	for( int p = 0; p < NUM_PHASES; ++p )
	{
		g_iSum[p] += g_iFrame[p];
		g_iWorst[p] = max( g_iWorst[p], g_iFrame[p] );
		iTotal += g_iFrame[p];
		g_iFrame[p] = 0;
	}
	g_iWorstTotal = max( g_iWorstTotal, iTotal );
	for( int p = 0; p < NUM_PARTS; ++p )
	{
		g_iSumPart[p] += g_iFramePart[p];
		g_iWorstPart[p] = max( g_iWorstPart[p], g_iFramePart[p] );
		g_iFramePart[p] = 0;
	}
	++g_iFrames;

	const Uint64 iFrequency = SDL_GetPerformanceFrequency();
	if( iNow - g_iPeriodStart < iFrequency )
		return;

	const double fMs = 1000.0 / iFrequency;
	const double fFrames = g_iFrames;
	const double fSeconds = double( iNow - g_iPeriodStart ) / iFrequency;

	Uint64 iSumTotal = 0;
	CString s;
	for( int p = 0; p < NUM_PHASES; ++p )
	{
		s += ssprintf( "%s %.3f/%.3f, ", PhaseNames[p], g_iSum[p]*fMs/fFrames, g_iWorst[p]*fMs );
		iSumTotal += g_iSum[p];
	}
	s += ssprintf( "total %.3f/%.3f", iSumTotal*fMs/fFrames, g_iWorstTotal*fMs );

	/* What the frames were up against.  The refresh rate is what SDL says the
	 * display does. */
	const RageDisplay::VideoModeParams params = DISPLAY->GetVideoModeParams();
	LOG->Info( "Frame stats (%s, vsync %s, display %.0f Hz): %d loops and %d frames drawn in %.2f s (%.0f and %.0f a second), ms per loop average/worst: %s; SDL_PumpEvents %.3f/%.3f in %.1f calls",
		params.windowed? "windowed":"fullscreen", params.vsync? "on":"off", DISPLAY->GetRefreshRate(),
		g_iFrames, g_iShown, fSeconds, g_iFrames / fSeconds, g_iShown / fSeconds, s.c_str(),
		g_iSumPart[PART_PUMP_EVENTS] / 1000000.0 / fFrames, g_iWorstPart[PART_PUMP_EVENTS] / 1000000.0,
		g_iPartCalls[PART_PUMP_EVENTS] / fFrames );

	memset( g_iSum, 0, sizeof(g_iSum) );
	memset( g_iWorst, 0, sizeof(g_iWorst) );
	memset( g_iSumPart, 0, sizeof(g_iSumPart) );
	memset( g_iWorstPart, 0, sizeof(g_iWorstPart) );
	memset( g_iPartCalls, 0, sizeof(g_iPartCalls) );
	g_iWorstTotal = 0;
	g_iFrames = g_iShown = 0;
	g_iPeriodStart = iNow;

	/* Don't count logging this against the next frame. */
	g_iLastMark = SDL_GetPerformanceCounter();
}
