/* Checks that threads asking for the time at once don't push it ahead.  The clock
 * every mutex lock and timestamp reads goes through ArchHooks::FixupTimeIfBackwards,
 * which adds to an offset when the time seems to have gone back, and a thread that
 * read the clock and was delayed before it got there saw just that.  Every time that
 * happened, all later times were a little further ahead for good. */
#include "global.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "test_misc.h"

#include <stdlib.h>

static volatile bool g_bStop = false;
static volatile int g_iBackwards[3];

static int Hammer( void *p )
{
	const int iThread = (int) (intptr_t) p;
	int64_t iLast = 0;
	while( !g_bStop )
	{
		const int64_t iNow = ArchHooks::GetMicrosecondsSinceStart( true );
		if( iNow < iLast )
			++g_iBackwards[iThread];
		iLast = iNow;
	}
	return 0;
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	ArchHooks::GetMicrosecondsSinceStart( true );

	RageThread threads[3];
	for( int i = 0; i < 3; ++i )
	{
		threads[i].SetName( ssprintf("Hammer %i", i) );
		threads[i].Create( Hammer, (void *) (intptr_t) i );
	}

	/* A few seconds of all three asking for the time as fast as they can. */
	const int64_t iStart = ArchHooks::GetMicrosecondsSinceStart( false );
	while( ArchHooks::GetMicrosecondsSinceStart(false) - iStart < 4000000 )
		usleep( 10000 );
	g_bStop = true;
	for( int i = 0; i < 3; ++i )
		threads[i].Wait();

	/* The same clock, with and without the fix up: with nothing to fix, they agree. */
	const int64_t iRaw = ArchHooks::GetMicrosecondsSinceStart( false );
	const int64_t iFixed = ArchHooks::GetMicrosecondsSinceStart( true );
	const int64_t iAhead = iFixed - iRaw;

	int iFailures = 0;
	if( iAhead > 1000 )
	{
		LOG->Warn( "FAIL: the time is %lld microseconds ahead of the clock after 4 seconds", (long long) iAhead );
		++iFailures;
	}
	for( int i = 0; i < 3; ++i )
		if( g_iBackwards[i] )
		{
			LOG->Warn( "FAIL: thread %i saw the time go backwards %i times", i, (int) g_iBackwards[i] );
			++iFailures;
		}

	if( iFailures )
	{
		test_deinit();
		exit( 1 );
	}
	LOG->Info( "test_time_threads: all passed (%lld microseconds ahead)", (long long) iAhead );
	test_deinit();
	exit( 0 );
}
