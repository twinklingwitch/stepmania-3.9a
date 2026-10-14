#include "global.h"

#include <unistd.h>

#if defined(WINDOWS)
#include "RageLog.h"
#include <windows.h>
#elif defined(DARWIN)
#include "RageLog.h"
#include <cstdlib>
#endif

#if defined(CRASH_HANDLER)
#include "archutils/Unix/CrashHandler.h"
#endif

void NORETURN sm_crash( const char *reason )
{
#if defined(CRASH_HANDLER)
	ForceCrashHandler( reason );
#elif defined(WINDOWS)
	/* There's no crash handler to run, so say what happened and die. */
	if( LOG )
	{
		LOG->Warn( "Fatal error: %s", reason );
		LOG->Flush();
	}
	MessageBoxA( NULL, reason, "StepMania has crashed", MB_OK|MB_ICONERROR );
#elif defined(DARWIN)
	/* No crash handler here either.  Log the reason, and abort so the system's
	 * crash reporter writes a backtrace (~/Library/Logs/DiagnosticReports). */
	if( LOG )
	{
		LOG->Warn( "Fatal error: %s", reason );
		LOG->Flush();
	}
	abort();
#else
	*(volatile char*)0=0;

	/* This isn't actually reached.  We just do this to convince the compiler that the
	 * function really doesn't return. */
	while(1);
#endif

	_exit( 1 );
}

/*
 * (c) 2004 Glenn Maynard
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
