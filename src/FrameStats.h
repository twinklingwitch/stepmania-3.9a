/* FrameStats - where the time of each frame of the game loop goes. */

#ifndef FRAME_STATS_H
#define FRAME_STATS_H

/*
 * With SM_FRAME_STATS=1 in the environment, the log says once a second how
 * many milliseconds a frame spent in each part of the game loop, on average
 * and at worst.  Each mark gives the time since the last one to a phase, so
 * the phases add up to the whole frame.  Without it, a mark is a test of a
 * flag and nothing more.
 */
namespace FrameStats
{
	enum Phase
	{
		PHASE_PACE,		// PaceFrame, waiting for the refresh with vsync
		PHASE_WINDOW,		// DISPLAY->Update: the window and its events
		PHASE_SOUND,		// SOUNDMAN and SOUND
		PHASE_SCREENS,		// SCREENMAN->Update
		PHASE_INPUT,		// reading and handling input
		PHASE_OTHER,		// the other managers, and the rest of the loop
		PHASE_DRAW,		// drawing the screens (with GL, that's the GL calls)
		PHASE_RENDER,		// the renderer finishing the frame: sending batched draws (GL), uploading and recording (SDL GPU)
		PHASE_PRESENT,		// swapping buffers (GL); waiting for the GPU and the window's buffer, and submitting (SDL GPU)
		PHASE_LIMIT_SLEEP,	// MaxFPS: sleeping
		PHASE_LIMIT_SPIN,	// MaxFPS: spinning
		PHASE_UNFOCUSED,	// sleeping because the window doesn't have focus
		NUM_PHASES
	};

	/* Time spent inside a phase that's also worth seeing on its own. */
	enum Part
	{
		PART_PUMP_EVENTS,	// SDL_PumpEvents, in the window and input phases
		NUM_PARTS
	};

	extern bool g_bEnabled;

	void DoMark( Phase p );
	void DoAddPart( Part p, unsigned long long iNanoseconds );
	void DoFrameShown();
	void DoEndFrame();

	/* The time since the last mark was spent in phase p. */
	inline void Mark( Phase p ) { if( g_bEnabled ) DoMark( p ); }
	inline void AddPart( Part p, unsigned long long iNanoseconds ) { if( g_bEnabled ) DoAddPart( p, iNanoseconds ); }

	/* The renderer finished a frame (a pass of the loop may not draw one). */
	inline void FrameShown() { if( g_bEnabled ) DoFrameShown(); }

	/* A pass of the game loop is over. */
	inline void EndFrame() { if( g_bEnabled ) DoEndFrame(); }
}

#endif
