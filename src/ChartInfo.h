/* ChartInfo - The text of the info lines a theme can turn on on the music
 * select and results screens: the music rate, a chart's length, BPM, note
 * counts, NPS and MSD, and the best score on it.  The lines about Wife3 (the
 * MSD and the Wife scores) are only asked for when the Wife preference is on. */

#ifndef CHART_INFO_H
#define CHART_INFO_H

#include "Grade.h"
#include "PlayerNumber.h"
#include "minacalc_c.h"

class Song;
class Steps;

namespace ChartInfo
{
	/* "1.0x", "1.1x", "1.05x". */
	CString RateString( float fRate );
	/* A Wife3 fraction as Etterna shows it: "96.12%", truncated. */
	CString PercentString( float fFraction );
	/* "J4", "Justice", or the scale itself for one that isn't a judge. */
	CString JudgeName( float fJudgeScale );

	/* The grade Etterna gives a Wife3 score, as 3.9's grades: AAAA from
	 * 99.955%, AAA from 99.7%, AA from 93%, A from 80%, B from 70%, C from
	 * 60%, D under that. */
	Grade WifeToGrade( float fWife );

	/* At the music rate: "1:52" from the start of the music to the last note,
	 * "BPM 150-180", and the average notes per second between the first note
	 * and the last. */
	CString LengthString( const Song *pSong, float fRate );
	CString BPMString( const Song *pSong, float fRate );
	CString NPSString( const Song *pSong, const Steps *pSteps, float fRate );
	/* "Notes 812  Jumps 45  Hands 3  Holds 22  Mines 0". */
	CString NotesString( const Steps *pSteps );

	/* From an MSD table at a rate (ChartMSD::GetMSDAtRate): "MSD 24.31", and
	 * the chart's two highest skillsets, "Stream 24.31  Jumpstream 22.10". */
	CString MSDString( const float fMSD[MINACALC_NUM_SKILLSETS] );
	CString SkillsetsString( const float fMSD[MINACALC_NUM_SKILLSETS] );

	/* The player's best score on the chart (the machine's if the player has no
	 * profile).  With Wife it's the best Wife3 score at the rate, or the best
	 * at any rate if there's none at this one ("Best 96.12% AA", "Best 95.20% AA
	 * at 1.2x"); it works out the chart key, which takes a moment.  Without
	 * Wife it's 3.9's top score ("Best AA 93.21%"). */
	CString BestScoreString( PlayerNumber pn, const Song *pSong, const Steps *pSteps, float fRate, bool bWife );
}

#endif
