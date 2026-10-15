/* WifeScores - The Wife3 scores of a profile, for the local leaderboards.
 *
 * They are kept in their own file next to Stats.xml (WifeScores.xml) and not in
 * Stats.xml, which only keeps the best few scores of a chart by 3.9's rules.
 * Every song played with Wife on is added, whether or not it made a 3.9 high
 * score list.  The replay of a score is in the Replays folder of the profile,
 * in a file named by the score key. */

#ifndef WIFE_SCORES_H
#define WIFE_SCORES_H

#include "DateTime.h"

struct XNode;

struct WifeScoreEntry
{
	CString sScoreKey;
	CString sChartKey;
	float fWife;		// as a fraction, at the judge it was played on
	float fRate;		// music rate
	float fJudgeScale;	// judge window scale: 1 is Judge 4
	CString sName;		// who played it
	CString sGrade;		// the grade 3.9 gave it
	CString sSong;		// what it was, for people to read
	DateTime dateTime;

	CString sFromDir;	// which profile it was loaded from; not saved

	WifeScoreEntry(): fWife(0), fRate(1), fJudgeScale(1) { }

	XNode *CreateNode() const;
	void LoadFromNode( const XNode *pNode );
};

namespace WifeScores
{
	/* Profile directories end in a slash. */
	CString GetScoresFile( const CString &sProfileDir );
	CString GetReplayDir( const CString &sProfileDir );

	/* Appends what the profile has to out. */
	void Load( const CString &sProfileDir, vector<WifeScoreEntry> &out );
	bool Add( const CString &sProfileDir, const WifeScoreEntry &e );
	/* The score as Load will read it back from the file, which is not always
	 * exactly what was added (numbers are written with six decimals).  False
	 * if Load would skip it. */
	bool AsLoaded( const WifeScoreEntry &e, const CString &sProfileDir, WifeScoreEntry &out );

	/* The scores of one chart at one rate (at every rate if fRate is negative),
	 * best first, with a score that is in more than one profile only once. */
	void ForChart( const vector<WifeScoreEntry> &all, const CString &sChartKey, float fRate, vector<WifeScoreEntry> &out );

	/* 1 for the best; 0 if the score isn't in the list. */
	int GetRank( const vector<WifeScoreEntry> &sorted, const CString &sScoreKey );
}

#endif
