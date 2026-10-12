/* Checks Wife3 against values worked out with the formula from Etterna
 * (RageUtil.h), done in double precision with the real erf:
 *
 *   ridic = 5*ts, zero = 65*ts^0.75, dev = 22.7*ts^0.75, boo = 180*ts
 *   |off| <= ridic:  2
 *   |off| <= zero:   2*erf((zero-|off|)/dev)
 *   |off| <= boo:    (|off|-zero) * -5.5 / (boo-zero)
 *   otherwise:       -5.5
 * Our version uses the A&S 7.1.26 approximation of erf, good to 1.5e-7. */
#include "global.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "Wife.h"
#include "WifeReplay.h"
#include "test_misc.h"

#include <math.h>

static int g_iFailures = 0;

static void Check( const char *szWhat, float fGot, float fExpected, float fTolerance )
{
	if( fabsf(fGot - fExpected) > fTolerance )
	{
		LOG->Warn( "FAIL %s: got %f, expected %f", szWhat, fGot, fExpected );
		++g_iFailures;
	}
}

static void TestWife3()
{
	static const struct
	{
		float ms, ts, expected;
	} t[] = {
		{ 0.0f, 1.00f, 2.000000f },
		{ 3.0f, 1.00f, 2.000000f },
		{ 5.0f, 1.00f, 2.000000f },
		{ 10.0f, 1.00f, 1.998777f },
		{ 22.5f, 1.00f, 1.983794f },
		{ 45.0f, 1.00f, 1.574474f },
		{ 65.0f, 1.00f, 0.000000f },
		{ 90.0f, 1.00f, -1.195652f },
		{ 135.0f, 1.00f, -3.347826f },
		{ 150.0f, 1.00f, -4.065217f },
		{ 180.0f, 1.00f, -5.500000f },
		{ 200.0f, 1.00f, -5.500000f },
		{ 5.0f, 0.84f, 1.999559f },
		{ 10.0f, 0.84f, 1.998321f },
		{ 22.5f, 0.84f, 1.971582f },
		{ 45.0f, 0.84f, 1.214181f },
		{ 65.0f, 0.84f, -0.465351f },
		{ 90.0f, 0.84f, -1.925516f },
		{ 135.0f, 0.84f, -4.553813f },
		{ 150.0f, 0.84f, -5.429912f },
		{ 10.0f, 1.50f, 1.999338f },
		{ 45.0f, 1.50f, 1.904844f },
		{ 65.0f, 1.50f, 1.423375f },
		{ 90.0f, 1.50f, -0.057413f },
		{ 180.0f, 1.50f, -2.778707f },
		{ 200.0f, 1.50f, -3.383439f },
		{ 3.0f, 0.20f, 1.998768f },
		{ 5.0f, 0.20f, 1.994740f },
		{ 10.0f, 0.20f, 1.901490f },
		{ 22.5f, 0.20f, -1.016430f },
		{ 45.0f, 0.20f, -5.500000f },
	};

	for( unsigned i=0; i<ARRAYSIZE(t); i++ )
	{
		char sz[64];
		sprintf( sz, "Wife3(%.1fms, %.2f)", t[i].ms, t[i].ts );
		/* Early and late score the same. */
		Check( sz, Wife3( t[i].ms/1000, t[i].ts ), t[i].expected, 0.0005f );
		Check( sz, Wife3( -t[i].ms/1000, t[i].ts ), t[i].expected, 0.0005f );
	}

	/* The erf approximation against a few known values of erf. */
	Check( "erf(0)", Wife3Erf(0), 0.0f, 0.00001f );
	Check( "erf(0.5)", Wife3Erf(0.5f), 0.5204998778f, 0.00001f );
	Check( "erf(1)", Wife3Erf(1), 0.8427007929f, 0.00001f );
	Check( "erf(-1)", Wife3Erf(-1), -0.8427007929f, 0.00001f );
	Check( "erf(2.5)", Wife3Erf(2.5f), 0.9995930480f, 0.00001f );
}

/* A short chart worked out by hand: four notes (one of them a hold head, 8
 * points in all) judged at Judge 4:
 *
 *   tap at 0ms          2
 *   tap at 10ms late    1.998777
 *   hold head at 45ms   1.574474
 *   tap missed         -5.5
 * then a mine is hit (-7) and the hold is dropped (-4.5).
 *
 * After the four notes: points 0.073251 over 8 judged = 0.00915637.
 * At the end: points -11.426749 over 8 = -1.42834. */
static void TestTally()
{
	WifeTally w;
	w.Reset( 8, 1.0f );
	Check( "live before anything", w.GetLivePercent(), 0.0f, 0.0f );

	w.Tap( 0.0f );
	Check( "live 1", w.GetLivePercent(), 1.0f, 0.00001f );
	w.Tap( 0.010f );
	w.Tap( -0.045f );
	w.Miss();
	Check( "points 4 notes", w.GetPoints(), 0.073251f, 0.001f );
	Check( "live 4 notes", w.GetLivePercent(), 0.00915637f, 0.0001f );

	w.Mine();
	w.HoldDrop();
	Check( "points end", w.GetPoints(), -11.426749f, 0.001f );
	/* Mines and drops cost points but don't count as judged notes. */
	Check( "live end", w.GetLivePercent(), -11.426749f/8, 0.0002f );
	Check( "final end", w.GetFinalPercent(), -1.42834f, 0.0002f );

	/* A run that stops early is scored against the whole chart. */
	w.Reset( 8, 1.0f );
	w.Tap( 0.0f );
	w.Tap( 0.0f );
	Check( "live early stop", w.GetLivePercent(), 1.0f, 0.00001f );
	Check( "final early stop", w.GetFinalPercent(), 0.5f, 0.00001f );

	Check( "truncate", Wife3Truncate( 0.99996f ), 99.99f, 0.0001f );
	Check( "truncate half", Wife3Truncate( 0.5f ), 50.0f, 0.0001f );
}

/* A replay with a bit of everything: three hits, a press that found no note, a
 * mine, a miss and a dropped hold.  The points worked out from it must be what
 * the tally gives for the same play, and it must come back the same from a file. */
static void TestReplay()
{
	WifeReplay r;
	r.sChartKey = "Xabc";
	r.sScoreKey = "Sdef";
	r.fRate = 1.2f;
	r.fSongOffset = -0.125f;
	r.fGlobalOffset = 0.01f;
	r.sMods = "Reverse, 2x";
	r.iRngSeed = 77;

	WifeInputEvent e;
	e.bPress = true;
	e.iColumn = 0; e.fSongSeconds = 1.0f; e.iNearestRow = 48; e.fOffset = 0.0f; e.iNearestType = WIFE_TYPE_TAP; e.iNearestSubType = WIFE_SUBTYPE_INVALID;
	r.inputs.push_back( e );
	e.iColumn = 3; e.fSongSeconds = 1.51f; e.iNearestRow = 96; e.fOffset = 0.010f;
	r.inputs.push_back( e );
	e.iColumn = 1; e.fSongSeconds = 2.0f; e.iNearestRow = 144; e.fOffset = -0.045f; e.iNearestType = WIFE_TYPE_HOLD_HEAD; e.iNearestSubType = WIFE_SUBTYPE_HOLD;
	r.inputs.push_back( e );
	/* a press with no note near it, and one too far from its note to count */
	e.iColumn = 2; e.fSongSeconds = 3.0f; e.iNearestRow = -1; e.fOffset = 0; e.iNearestType = WIFE_TYPE_INVALID; e.iNearestSubType = WIFE_SUBTYPE_INVALID;
	r.inputs.push_back( e );
	e.iColumn = 2; e.fSongSeconds = 3.5f; e.iNearestRow = 192; e.fOffset = 0.25f; e.iNearestType = WIFE_TYPE_TAP; e.iNearestSubType = WIFE_SUBTYPE_INVALID;
	r.inputs.push_back( e );
	/* the mine */
	e.iColumn = 1; e.fSongSeconds = 4.0f; e.iNearestRow = 240; e.fOffset = 0.02f; e.iNearestType = WIFE_TYPE_MINE;
	r.inputs.push_back( e );
	WifeMineHit m = { 240, 1 };
	r.mines.push_back( m );
	WifeMiss x = { 0, 288, WIFE_TYPE_TAP, WIFE_SUBTYPE_INVALID };
	r.misses.push_back( x );
	WifeHoldDrop h = { 144, 1, WIFE_SUBTYPE_HOLD };
	r.holds.push_back( h );

	WifeTally w;
	w.Reset( 10, 1.0f );
	w.Tap( 0.0f );
	w.Tap( 0.010f );
	w.Tap( -0.045f );
	w.Mine();
	w.Miss();
	w.HoldDrop();
	Check( "replay points", r.GetPoints(1.0f), w.GetPoints(), 0.0001f );
	Check( "replay notes", (float) r.GetNumNotes(1.0f), 4.0f, 0.0f );
	/* the press 250ms off is a hit with a window 1.5 times as wide, though not one that scores */
	Check( "replay notes at J1", (float) r.GetNumNotes(1.5f), 5.0f, 0.0f );

	FILEMAN->CreateDir( "wifetest" );
	if( !r.Write( "wifetest/replay" ) )
	{
		LOG->Warn( "FAIL: couldn't write the replay" );
		++g_iFailures;
		return;
	}
	WifeReplay back;
	if( !back.Read( "wifetest/replay" ) )
	{
		LOG->Warn( "FAIL: couldn't read the replay back" );
		++g_iFailures;
		return;
	}
	if( back.ToString() != r.ToString() )
	{
		LOG->Warn( "FAIL: replay changed:\n%s\n%s", r.ToString().c_str(), back.ToString().c_str() );
		++g_iFailures;
	}
	Check( "reread points", back.GetPoints(1.0f), w.GetPoints(), 0.0001f );
	Check( "reread rate", back.fRate, 1.2f, 0.0001f );
	if( back.sMods != "Reverse,2x"  ||  back.iRngSeed != 77  ||  back.inputs.size() != 6  ||  back.mines.size() != 1
		||  back.misses.size() != 1  ||  back.holds.size() != 1 )
	{
		LOG->Warn( "FAIL: replay fields changed" );
		++g_iFailures;
	}
	FILEMAN->Remove( "wifetest/replay" );
	FILEMAN->Remove( "wifetest" );

	/* Etterna's header has to have eight fields, and a damaged file is refused. */
	WifeReplay bad;
	if( bad.FromString( "Xabc Sdef 1.0 0 0 none 0\n" ) || bad.FromString( "" ) )
	{
		LOG->Warn( "FAIL: bad replays were taken" );
		++g_iFailures;
	}
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	TestWife3();
	TestTally();
	TestReplay();

	if( g_iFailures )
		LOG->Warn( "%i checks failed", g_iFailures );
	else
		LOG->Info( "wife: all checks passed" );

	test_deinit();
	return g_iFailures? 1: 0;
}
