#include "global.h"
#include "OnlineRules.h"
#include "ChartMSD.h"
#include "NoteData.h"

#include <math.h>

OnlinePlaySetup::OnlinePlaySetup()
{
	bRegularPlay = true;
	bHuman = true;
	bDanceSingle = true;
	bAutogen = false;
	fRate = 1;
	fJudgeScale = 1;
	bDefaultWindows = true;
	bUnrankedTurn = false;
	bTransform = false;
	bRandomVanish = false;
	bAutoSync = false;
	bDefaultInput = true;
}

float OnlineRules::RoundRate( float fRate )
{
	return floorf( fRate * 100 + 0.5f ) / 100;
}

void OnlineRules::CheckSetup( const OnlinePlaySetup &s, vector<Rule> &out )
{
	if( !s.bRegularPlay )
		out.push_back( PLAY_MODE );
	if( !s.bHuman )
		out.push_back( PLAYER );
	if( !s.bDanceSingle )
		out.push_back( STEPS_TYPE );
	if( s.bAutogen )
		out.push_back( AUTOGEN );

	const float fRate = RoundRate( s.fRate );
	if( fRate < 0.7f || fRate > 2.0f )
		out.push_back( RATE );
	if( s.fJudgeScale > 1.0f )
		out.push_back( JUDGE_SCALE );
	if( !s.bDefaultWindows )
		out.push_back( JUDGE_WINDOWS );

	if( s.bUnrankedTurn )
		out.push_back( TURNS );
	if( s.bTransform )
		out.push_back( TRANSFORMS );
	if( s.bRandomVanish )
		out.push_back( RANDOM_VANISH );
	if( s.bAutoSync )
		out.push_back( AUTOSYNC );
	if( !s.bDefaultInput )
		out.push_back( INPUT_SETTINGS );
}

void OnlineRules::CheckNotes( const NoteData &nd, vector<Rule> &out )
{
	bool bAttacks = false, bStray = false;
	for( int t = 0; t < nd.GetNumTracks(); ++t )
		for( int r = 0; r < nd.GetNumRows(); ++r )
		{
			const TapNote::Type type = (TapNote::Type) nd.GetTapNoteX(t, r).type;
			if( type == TapNote::attack && !bAttacks )
			{
				bAttacks = true;
				out.push_back( ATTACKS );
			}
			/* A hold end without a head ('3' with no '2') stays in 3.9's notes as a
			 * note of its own, which is judged like a tap but isn't in the export's
			 * notes.  A roll of newer simfiles ('4' ... '3') loads as one: 3.9 drops
			 * the '4' it doesn't know. */
			else if( (type == TapNote::hold_tail || type == TapNote::hold) && !bStray )
			{
				bStray = true;
				out.push_back( STRAY_HOLD_ENDS );
			}
			if( bAttacks && bStray )
				return;
		}
}

bool OnlineRules::IsRatedByCalc( const vector<minacalc_row> &rows, const ChartMSDTable &msd )
{
	/* Row 3 of the table is 1.0x. */
	return rows.size() >= 2 && msd.fMSD[3][MINACALC_OVERALL] > 0;
}

bool OnlineRules::IsChartRule( Rule r )
{
	switch( r )
	{
	case STEPS_TYPE:
	case AUTOGEN:
	case ATTACKS:
	case STRAY_HOLD_ENDS:
	case NOT_RATED:
		return true;
	default:
		return false;
	}
}

const char *OnlineRules::GetReason( Rule r )
{
	switch( r )
	{
	case PLAY_MODE:			return "only single songs in regular play are ranked";
	case PLAYER:			return "autoplay";
	case STEPS_TYPE:
	case AUTOGEN:			return "only 4-key dance-single charts are ranked";
	case ATTACKS:			return "the chart has attacks";
	case STRAY_HOLD_ENDS:	return "the chart has hold ends without a start";
	case NOT_RATED:			return "the difficulty calculator doesn't rate the chart";
	case RATE:				return "the music rate must be between 0.7x and 2.0x";
	case JUDGE_SCALE:		return "judges easier than Judge 4 aren't ranked";
	case JUDGE_WINDOWS:		return "the judge windows aren't 3.9's";
	case TURNS:				return "turns other than Mirror aren't ranked";
	case TRANSFORMS:		return "options that add or remove notes aren't ranked";
	case RANDOM_VANISH:		return "RandomVanish isn't ranked";
	case AUTOSYNC:			return "AutoSync";
	case INPUT_SETTINGS:	return "PadStickSeconds and ConstantUpdateDeltaSeconds must be 0";
	default:				ASSERT(0); return "";
	}
}

const char *OnlineRules::GetExplanation( Rule r )
{
	switch( r )
	{
	case PLAY_MODE:
		return "This isn't a single song in regular play; courses, battle and rave aren't ranked.";
	case PLAYER:
		return "AutoPlay is on, or the computer plays this side; only a person's plays are ranked.";
	case STEPS_TYPE:
		return "This isn't a 4-key dance-single chart; only those are ranked.";
	case AUTOGEN:
		return "The game made this chart from another steps type (AutogenSteps); only charts in the simfile are ranked.";
	case ATTACKS:
		return "This chart has attacks; the server doesn't rank charts with attacks.";
	case STRAY_HOLD_ENDS:
		return "This chart has hold ends without a start (3.9 reads the rolls of newer simfiles that way); "
			"the server doesn't rank charts with them.";
	case NOT_RATED:
		return "The difficulty calculator gives this chart no rating (too few notes, for one); "
			"the server only ranks charts it rates.";
	case RATE:
		return "Only music rates from 0.7x to 2.0x are ranked.";
	case JUDGE_SCALE:
		return "The judge is easier than Judge 4 (JudgeWindowScale above 1); only Judge 4 and harder are ranked.";
	case JUDGE_WINDOWS:
		return "JudgeWindowAdd or a JudgeWindowSeconds setting isn't 3.9's default; only the default windows are ranked.";
	case TURNS:
		return "Left, Right, Shuffle and SuperShuffle aren't ranked; of the turns, only Mirror is.";
	case TRANSFORMS:
		return "An option that adds or removes notes is on (Little, NoHolds, Mines, Echo and the like); those aren't ranked.";
	case RANDOM_VANISH:
		return "RandomVanish is on; it moves notes during play, so it isn't ranked.";
	case AUTOSYNC:
		return "AutoSync is on; it isn't ranked.";
	case INPUT_SETTINGS:
		return "PadStickSeconds or ConstantUpdateDeltaSeconds isn't 0 in StepMania.ini; only 0 is ranked.";
	default:
		ASSERT(0); return "";
	}
}
