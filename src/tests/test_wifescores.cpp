/* Checks the Wife score files: that scores written come back, that a chart's
 * leaderboard is ordered best first across profiles, that the same score in two
 * profiles is listed once, and that the rate keeps scores apart. */
#include "global.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageUtil.h"
#include "WifeScores.h"
#include "test_misc.h"

#include <math.h>

static int g_iFailures = 0;

static void Fail( const char *szWhat )
{
	LOG->Warn( "FAIL: %s", szWhat );
	++g_iFailures;
}

static WifeScoreEntry Make( const char *szKey, const char *szChart, float fWife, float fRate, const char *szName, int iDay )
{
	WifeScoreEntry e;
	e.sScoreKey = szKey;
	e.sChartKey = szChart;
	e.fWife = fWife;
	e.fRate = fRate;
	e.fJudgeScale = 1.0f;
	e.sName = szName;
	e.sGrade = "Tier01";
	e.sSong = "Song <&> \"quoted\"";
	e.dateTime.tm_year = 126;
	e.dateTime.tm_mon = 9;
	e.dateTime.tm_mday = iDay;
	return e;
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	FILEMAN->CreateDir( "wifetest" );
	FILEMAN->CreateDir( "wifetest/alice" );
	FILEMAN->CreateDir( "wifetest/bob" );
	FILEMAN->CreateDir( "wifetest/machine" );
	const CString sAlice = "wifetest/alice/", sBob = "wifetest/bob/", sMachine = "wifetest/machine/";

	/* Alice and Bob each play the chart X1; the machine has both plays. */
	WifeScoreEntry a1 = Make( "Sa1", "X1", 0.9500f, 1.0f, "Alice", 1 );
	WifeScoreEntry a2 = Make( "Sa2", "X1", 0.9812f, 1.0f, "Alice", 2 );
	WifeScoreEntry a3 = Make( "Sa3", "X1", 0.9900f, 1.2f, "Alice", 3 );
	WifeScoreEntry a4 = Make( "Sa4", "X2", 0.7000f, 1.0f, "Alice", 4 );
	WifeScoreEntry b1 = Make( "Sb1", "X1", 0.9700f, 1.0f, "Bob", 5 );
	WifeScoreEntry b2 = Make( "Sb2", "X1", 0.9812f, 1.0f, "Bob", 6 );	// ties Alice's best, later
	WifeScoreEntry b3 = Make( "Sb3", "X1", -0.2500f, 1.0f, "Bob", 7 );	// a bad one can be negative

	if( !WifeScores::Add(sAlice, a1) || !WifeScores::Add(sAlice, a2) || !WifeScores::Add(sAlice, a3) || !WifeScores::Add(sAlice, a4) )
		Fail( "adding to Alice's file" );
	if( !WifeScores::Add(sBob, b1) || !WifeScores::Add(sBob, b2) || !WifeScores::Add(sBob, b3) )
		Fail( "adding to Bob's file" );
	const WifeScoreEntry all[] = { a1, a2, a3, a4, b1, b2, b3 };
	for( unsigned i=0; i<ARRAYSIZE(all); i++ )
		if( !WifeScores::Add(sMachine, all[i]) )
			Fail( "adding to the machine's file" );

	vector<WifeScoreEntry> alice, bob, machine;
	WifeScores::Load( sAlice, alice );
	WifeScores::Load( sBob, bob );
	WifeScores::Load( sMachine, machine );
	if( alice.size() != 4 || bob.size() != 3 || machine.size() != 7 )
		Fail( "number of scores read back" );
	else if( alice[2].sScoreKey != "Sa3" || fabsf(alice[2].fWife - 0.99f) > 0.0001f || fabsf(alice[2].fRate - 1.2f) > 0.0001f
		|| alice[2].sName != "Alice" || alice[2].sSong != "Song <&> \"quoted\"" || alice[2].dateTime.tm_mday != 3
		|| alice[2].sFromDir != sAlice )
		Fail( "fields read back" );

	/* One profile's board: Alice's X1 at 1x is 0.9812 then 0.95. */
	vector<WifeScoreEntry> v;
	WifeScores::ForChart( alice, "X1", 1.0f, v );
	if( v.size() != 2 || v[0].sScoreKey != "Sa2" || v[1].sScoreKey != "Sa1" )
		Fail( "Alice's board" );

	/* The rate matters: at 1.2x she has one score, at 1.5x none. */
	v.clear();
	WifeScores::ForChart( alice, "X1", 1.2f, v );
	if( v.size() != 1 || v[0].sScoreKey != "Sa3" )
		Fail( "Alice at 1.2x" );
	v.clear();
	WifeScores::ForChart( alice, "X1", 1.5f, v );
	if( !v.empty() )
		Fail( "Alice at 1.5x" );

	/* All profiles: both players' files plus the machine's, which has the same
	 * plays again.  Each score once; the tie goes to the one played first. */
	vector<WifeScoreEntry> everything;
	everything.insert( everything.end(), alice.begin(), alice.end() );
	everything.insert( everything.end(), bob.begin(), bob.end() );
	everything.insert( everything.end(), machine.begin(), machine.end() );
	v.clear();
	WifeScores::ForChart( everything, "X1", 1.0f, v );
	const char *aszExpected[] = { "Sa2", "Sb2", "Sb1", "Sa1", "Sb3" };
	if( v.size() != 5 )
		Fail( "size of the board across profiles" );
	else
		for( unsigned i=0; i<5; i++ )
			if( v[i].sScoreKey != aszExpected[i] )
			{
				LOG->Warn( "place %u is %s, expected %s", i+1, v[i].sScoreKey.c_str(), aszExpected[i] );
				Fail( "order of the board across profiles" );
			}
	if( WifeScores::GetRank(v, "Sb1") != 3 || WifeScores::GetRank(v, "Sb2") != 2 || WifeScores::GetRank(v, "nope") != 0 )
		Fail( "ranks" );

	/* Another chart doesn't mix in. */
	v.clear();
	WifeScores::ForChart( everything, "X2", 1.0f, v );
	if( v.size() != 1 || v[0].sScoreKey != "Sa4" )
		Fail( "other chart" );

	/* A directory without a file has no scores, and that's not a problem. */
	vector<WifeScoreEntry> none;
	WifeScores::Load( "wifetest/nobody/", none );
	if( !none.empty() )
		Fail( "scores from nowhere" );

	const CString dirs[] = { sAlice, sBob, sMachine };
	for( unsigned i=0; i<3; i++ )
	{
		FILEMAN->Remove( WifeScores::GetScoresFile(dirs[i]) );
		FILEMAN->Remove( dirs[i] );
	}
	FILEMAN->Remove( "wifetest" );

	if( g_iFailures )
		LOG->Warn( "%i checks failed", g_iFailures );
	else
		LOG->Info( "wifescores: all checks passed" );

	test_deinit();
	return g_iFailures? 1: 0;
}
