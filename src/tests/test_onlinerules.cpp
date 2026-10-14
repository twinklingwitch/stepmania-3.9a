/* Checks the online validity rules (OnlineRules): each setting that can't be
 * ranked is caught, the rate is rounded as the server rounds it, the notes'
 * rules find attacks and hold ends without a start (as a roll of a newer
 * simfile loads in 3.9) in the order the chart export reports them, and a
 * chart the calculator doesn't rate is unranked. */
#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "ChartMSD.h"
#include "NoteData.h"
#include "OnlineRules.h"
#include "test_misc.h"

#include <stdlib.h>
#include <string.h>

static int g_iFailures = 0;

static void Check( bool b, const char *szWhat )
{
	if( b )
		return;
	LOG->Warn( "FAIL: %s", szWhat );
	++g_iFailures;
}

/* The rules the setup breaks, as one rule or NUM_RULES for none; -1 for more than one. */
static int SetupRule( const OnlinePlaySetup &s )
{
	vector<OnlineRules::Rule> v;
	OnlineRules::CheckSetup( s, v );
	if( v.empty() )
		return OnlineRules::NUM_RULES;
	return v.size() == 1? v[0]: -1;
}

static bool HasRule( const vector<OnlineRules::Rule> &v, OnlineRules::Rule r )
{
	return find( v.begin(), v.end(), r ) != v.end();
}

static void TestSetup()
{
	const OnlinePlaySetup ok;
	Check( SetupRule(ok) == OnlineRules::NUM_RULES, "the default setup is ranked" );

	OnlinePlaySetup s;
	s.bRegularPlay = false;
	Check( SetupRule(s) == OnlineRules::PLAY_MODE, "battle, rave and courses" );
	s = ok; s.bHuman = false;
	Check( SetupRule(s) == OnlineRules::PLAYER, "autoplay" );
	s = ok; s.bDanceSingle = false;
	Check( SetupRule(s) == OnlineRules::STEPS_TYPE, "steps type" );
	s = ok; s.bAutogen = true;
	Check( SetupRule(s) == OnlineRules::AUTOGEN, "autogen" );
	s = ok; s.fJudgeScale = 1.01f;
	Check( SetupRule(s) == OnlineRules::JUDGE_SCALE, "judges easier than J4" );
	s = ok; s.fJudgeScale = 0.33f;
	Check( SetupRule(s) == OnlineRules::NUM_RULES, "harder judges are ranked" );
	s = ok; s.bDefaultWindows = false;
	Check( SetupRule(s) == OnlineRules::JUDGE_WINDOWS, "judge windows" );
	s = ok; s.bUnrankedTurn = true;
	Check( SetupRule(s) == OnlineRules::TURNS, "turns" );
	s = ok; s.bTransform = true;
	Check( SetupRule(s) == OnlineRules::TRANSFORMS, "transforms" );
	s = ok; s.bRandomVanish = true;
	Check( SetupRule(s) == OnlineRules::RANDOM_VANISH, "RandomVanish" );
	s = ok; s.bAutoSync = true;
	Check( SetupRule(s) == OnlineRules::AUTOSYNC, "AutoSync" );
	s = ok; s.bDefaultInput = false;
	Check( SetupRule(s) == OnlineRules::INPUT_SETTINGS, "PadStickSeconds" );

	/* The rate is taken to two decimals, as the server does. */
	s = ok; s.fRate = 0.7f;
	Check( SetupRule(s) == OnlineRules::NUM_RULES, "0.7x is ranked" );
	s.fRate = 2.0f;
	Check( SetupRule(s) == OnlineRules::NUM_RULES, "2.0x is ranked" );
	s.fRate = 2.004f;
	Check( SetupRule(s) == OnlineRules::NUM_RULES, "2.004x is 2.00x" );
	s.fRate = 2.006f;
	Check( SetupRule(s) == OnlineRules::RATE, "2.006x is 2.01x" );
	s.fRate = 0.69f;
	Check( SetupRule(s) == OnlineRules::RATE, "0.69x" );
	s.fRate = 0.5f;
	Check( SetupRule(s) == OnlineRules::RATE, "0.5x" );

	/* Everything at once, in order. */
	s.bRegularPlay = false;
	s.bUnrankedTurn = true;
	s.bAutoSync = true;
	vector<OnlineRules::Rule> v;
	OnlineRules::CheckSetup( s, v );
	Check( v.size() == 4 && v[0] == OnlineRules::PLAY_MODE && v[1] == OnlineRules::RATE &&
		v[2] == OnlineRules::TURNS && v[3] == OnlineRules::AUTOSYNC, "several rules in order" );
}

static void TestNotes()
{
	NoteData nd;
	nd.SetNumTracks( 4 );
	nd.SetTapNote( 0, BeatToNoteRow(0), TAP_ORIGINAL_TAP );
	nd.SetTapNote( 1, BeatToNoteRow(1), TAP_ORIGINAL_MINE );
	nd.AddHoldNote( HoldNote(2, BeatToNoteRow(1), BeatToNoteRow(2)) );
	vector<OnlineRules::Rule> v;
	OnlineRules::CheckNotes( nd, v );
	Check( v.empty(), "taps, mines and holds are ranked" );

	/* A hold in column 2 ('2' then '3') and a roll in column 3 ('4' then '3'):
	 * 3.9 doesn't know the roll's '4' and drops it, so its end is left without
	 * a start. */
	NoteData roll;
	roll.SetNumTracks( 4 );
	roll.SetTapNote( 0, BeatToNoteRow(0), TAP_ORIGINAL_TAP );
	roll.SetTapNote( 2, BeatToNoteRow(1), TAP_ORIGINAL_HOLD_HEAD );
	roll.SetTapNote( 2, BeatToNoteRow(2), TAP_ORIGINAL_HOLD_TAIL );
	roll.SetTapNote( 3, BeatToNoteRow(2), TAP_ORIGINAL_HOLD_TAIL );
	roll.Convert2sAnd3sToHoldNotes();
	Check( roll.GetNumHoldNotes() == 1, "the hold is a hold" );
	v.clear();
	OnlineRules::CheckNotes( roll, v );
	Check( v.size() == 1 && v[0] == OnlineRules::STRAY_HOLD_ENDS, "a roll's end is a stray hold end" );

	/* Both, in the order the export finds them: column by column. */
	TapNote attack;
	attack.Set( TapNote::attack, TapNote::original, 0 );
	roll.SetTapNote( 1, BeatToNoteRow(3), attack );
	v.clear();
	OnlineRules::CheckNotes( roll, v );
	Check( v.size() == 2 && v[0] == OnlineRules::ATTACKS && v[1] == OnlineRules::STRAY_HOLD_ENDS, "attacks found first" );

	NoteData attacks;
	attacks.SetNumTracks( 4 );
	attacks.SetTapNote( 0, BeatToNoteRow(4), attack );
	attacks.SetTapNote( 3, BeatToNoteRow(4), attack );
	v.clear();
	OnlineRules::CheckNotes( attacks, v );
	Check( v.size() == 1 && v[0] == OnlineRules::ATTACKS, "attacks once" );
}

static void TestCalc()
{
	ChartMSDTable msd;
	memset( &msd, 0, sizeof(msd) );
	vector<minacalc_row> rows( 2 );
	Check( !OnlineRules::IsRatedByCalc(rows, msd), "no rating" );
	msd.fMSD[3][MINACALC_OVERALL] = 12.5f;
	Check( OnlineRules::IsRatedByCalc(rows, msd), "a rating at 1.0x" );
	rows.resize( 1 );
	Check( !OnlineRules::IsRatedByCalc(rows, msd), "one row" );
	msd.fMSD[3][MINACALC_OVERALL] = 0;
	msd.fMSD[4][MINACALC_OVERALL] = 12.5f;
	rows.resize( 2 );
	Check( !OnlineRules::IsRatedByCalc(rows, msd), "only 1.0x counts" );
}

static void TestTexts()
{
	for( int i = 0; i < OnlineRules::NUM_RULES; ++i )
	{
		const OnlineRules::Rule r = (OnlineRules::Rule) i;
		Check( strlen(OnlineRules::GetReason(r)) > 0, "every rule has a reason" );
		const CString s = OnlineRules::GetExplanation( r );
		Check( !s.empty() && s[s.size()-1] == '.', "every rule has a sentence" );
	}

	vector<OnlineRules::Rule> v;
	OnlinePlaySetup s;
	s.bDanceSingle = false;
	s.bTransform = true;
	OnlineRules::CheckSetup( s, v );
	Check( HasRule(v, OnlineRules::STEPS_TYPE) && OnlineRules::IsChartRule(OnlineRules::STEPS_TYPE), "steps type is the chart's" );
	Check( HasRule(v, OnlineRules::TRANSFORMS) && !OnlineRules::IsChartRule(OnlineRules::TRANSFORMS), "transforms are the play's" );
	Check( !OnlineRules::IsChartRule(OnlineRules::RATE), "the rate is the play's" );
}

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	TestSetup();
	TestNotes();
	TestCalc();
	TestTexts();

	if( g_iFailures )
	{
		LOG->Warn( "%i failures", g_iFailures );
		exit( 1 );
	}
	LOG->Info( "test_onlinerules: all passed" );
	exit( 0 );
}
