#include "global.h"
#include "ArchHooks.h"

ArchHooks *HOOKS = NULL;

#include "../arch_platform.h"
ArchHooks *MakeArchHooks() { return new ARCH_HOOKS; }

/*
 * This is a helper for GetMicrosecondsSinceStart on systems with a system
 * timer that may move backwards (eg. system clock and ntpd).
 *
 * If the system clock moves backwards, we can't just clamp the time; if it moved back
 * an hour, we'd sit around for an hour until it catches up.
 *
 * Keep track of an offset: the amount of time to add to the result.  If we move back
 * by 100ms, the offset will be increased by 100ms.
 *
 * This helper only needs to be used if the clock can move backwards.
 * If the underlying timer is reliable, this doesn't need to be used (for a small
 * efficiency bonus).  Also, you may omit this for GetMicrosecondsSinceStart() when
 * bAccurate == false.
 */

/* A reading older than the last by less than this is one that was taken before
 * another thread's and delivered after it. */
static const uint64_t MAX_STALE_READING_US = 1000000;

uint64_t ArchHooks::FixupTimeIfBackwards( uint64_t usecs )
{
	static uint64_t last = 0;
	static uint64_t offset_us = 0;

	if( usecs < last )
	{
		/* Every thread that asks for the time comes through here (every lock of a
		 * mutex does), and one that read the clock just before another and was then
		 * held up before getting here finds the time "moved backwards" by as long as
		 * it was held up.  Adding that to the offset would put every later time ahead
		 * for good, a little more each time it happens.  A clock that really goes
		 * back goes back by more than that. */
		const uint64_t iBackwards = last - usecs;
		if( iBackwards < MAX_STALE_READING_US )
			return last + offset_us;

		/* The time has moved backwards.  Increase the offset by the amount we moved. */
		offset_us += iBackwards;
	}

	last = usecs;

	return usecs + offset_us;
}

/*
 * (c) 2003-2004 Glenn Maynard, Chris Danford
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
