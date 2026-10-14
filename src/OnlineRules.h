/* OnlineRules - Which plays the online server ranks: the validity rules of
 * section 10.1 of docs/API.md (in the server's repository) that can be checked
 * before a song starts, and the chart export's rules (section 4.1) for which
 * charts are ranked at all.  Gameplay checks them so a play that can't be
 * ranked doesn't ask for a ticket and the results say why; the online panel on
 * the music select screen, so the player knows before picking the chart.  The
 * server checks all of them again.
 *
 * Nothing here looks at the game's state: OnlineManager::GetPlaySetup fills in
 * what the rules need, so they can be tested on their own. */

#ifndef ONLINE_RULES_H
#define ONLINE_RULES_H

#include "minacalc_c.h"

class NoteData;
struct ChartMSDTable;

/* What the rules look at, as the game has it before a song starts. */
struct OnlinePlaySetup
{
	bool bRegularPlay;		// a single song in regular play: no course, battle, rave or demonstration
	bool bHuman;			// a person plays: not the CPU, no AutoPlay
	bool bDanceSingle;		// the chart is 4-key dance-single
	bool bAutogen;			// the game made the chart from another steps type
	float fRate;			// the music rate
	float fJudgeScale;		// JudgeWindowScale
	bool bDefaultWindows;	// JudgeWindowAdd is 0 and every JudgeWindowSeconds* is 3.9's
	bool bUnrankedTurn;		// Left, Right, Shuffle or SuperShuffle
	bool bTransform;		// an option that adds or removes notes
	bool bRandomVanish;
	bool bAutoSync;
	bool bDefaultInput;		// PadStickSeconds and ConstantUpdateDeltaSeconds are 0

	/* A play that can be ranked. */
	OnlinePlaySetup();
};

namespace OnlineRules
{
	/* The rules, in the order they're checked. */
	enum Rule
	{
		PLAY_MODE,
		PLAYER,
		STEPS_TYPE,
		AUTOGEN,
		ATTACKS,
		STRAY_HOLD_ENDS,
		NOT_RATED,
		RATE,
		JUDGE_SCALE,
		JUDGE_WINDOWS,
		TURNS,
		TRANSFORMS,
		RANDOM_VANISH,
		AUTOSYNC,
		INPUT_SETTINGS,
		NUM_RULES
	};

	/* The rules of the setup, in order.  Adds what's broken to out. */
	void CheckSetup( const OnlinePlaySetup &s, vector<Rule> &out );
	/* The rules of the notes, in the order the chart export finds them
	 * (column by column): ATTACKS and STRAY_HOLD_ENDS. */
	void CheckNotes( const NoteData &nd, vector<Rule> &out );
	/* NOT_RATED: the export ranks no chart MinaCalc gives no rating at 1.0x. */
	bool IsRatedByCalc( const vector<minacalc_row> &rows, const ChartMSDTable &msd );

	/* The rate as the server takes it, to two decimals. */
	float RoundRate( float fRate );

	/* A rule of the chart itself, rather than of how it's played: the server
	 * doesn't rank the chart, whatever the options. */
	bool IsChartRule( Rule r );

	/* Short, for "Online: unranked, ...": "the chart has attacks". */
	const char *GetReason( Rule r );
	/* A sentence that says what's wrong and what would be ranked, for the
	 * online panel. */
	const char *GetExplanation( Rule r );
}

#endif
