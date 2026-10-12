/* ChartMSD - Etterna's difficulty ratings of charts, from MinaCalc (see
 * extern/MinaCalc): the MSD table of a chart (every skillset at every rate
 * Etterna caches) and the SSRs of a score.
 *
 * Only 4-key dance-single charts that aren't autogen are rated, as in Etterna's
 * rankings.  Nothing here runs unless something asks for it: the Wife panels,
 * the online scores, and Discord Rich Presence, which shows the MSD of the
 * chart being played.
 *
 * Use from the main thread: it loads the chart's notes, which isn't thread
 * safe.  ChartMSD::Compute only needs the rows, so it can run anywhere. */

#ifndef CHART_MSD_H
#define CHART_MSD_H

#include "minacalc_c.h"

class Song;
class Steps;

struct ChartMSDTable
{
	/* fMSD[i][s] is skillset s at rate (i+7)/10. */
	float fMSD[MINACALC_NUM_RATES][MINACALC_NUM_SKILLSETS];

	/* At any rate, as Etterna's Steps::GetMSD: in between cached rates it
	 * interpolates, and outside 0.7 to 2.0 it extrapolates. */
	float Get( float fRate, int iSkillset ) const;
};

namespace ChartMSD
{
	int GetCalcVersion();
	/* "Overall", "Stream", ... "Technical", as Etterna names them. */
	const char *SkillsetName( int iSkillset );

	bool IsSupported( const Steps *pSteps );
	/* 4 for dance-single. */
	unsigned GetKeyCount( const Steps *pSteps );

	/* The calc rows of a chart (see CalcRows.h).  False if it isn't supported. */
	bool GetRows( const Song *pSong, const Steps *pSteps, vector<minacalc_row> &out );

	/* Runs the calculator; false only if it failed.  Takes a while (tens of
	 * milliseconds for a long chart). */
	bool Compute( const vector<minacalc_row> &rows, unsigned iKeyCount, ChartMSDTable &out );
	bool ComputeSSR( const vector<minacalc_row> &rows, unsigned iKeyCount, float fRate, float fGoal, float out[MINACALC_NUM_SKILLSETS] );

	/* The MSD table of a chart, worked out once per chart key and kept in
	 * memory and in the song cache. */
	bool GetMSD( const Song *pSong, const Steps *pSteps, ChartMSDTable &out );

	/* The chart's MSD in every skillset at any music rate.  From 0.7 to 2.0 and
	 * above that is ChartMSDTable::Get on the chart's table; under 0.7, which
	 * the table doesn't cover, MinaCalc is asked for that rate (once per chart
	 * key and rate, kept in memory only).  That can take a few milliseconds to
	 * tens of them, like GetMSD, so don't ask every frame. */
	bool GetMSDAtRate( const Song *pSong, const Steps *pSteps, float fRate, float out[MINACALC_NUM_SKILLSETS] );

	/* The SSRs of a score of fGoal (a Wife3 fraction) at fRate. */
	bool GetSSR( const Song *pSong, const Steps *pSteps, float fRate, float fGoal, float out[MINACALC_NUM_SKILLSETS] );

	/* For working the MSD out elsewhere (Discord Rich Presence does it on its
	 * own thread): LookupMSD is GetMSD without running the calculator, from
	 * memory or the song cache, and StoreMSD keeps a table worked out elsewhere,
	 * in memory and, if bSongCache, in the song cache, so GetMSD doesn't do it
	 * again.  Both are for the main thread. */
	bool LookupMSD( const CString &sChartKey, unsigned uRowsHash, ChartMSDTable &out );
	void StoreMSD( const CString &sChartKey, unsigned uRowsHash, const ChartMSDTable &table, bool bSongCache );

	/* A hash of rows, to tell when they change for the same chart key. */
	unsigned HashRows( const vector<minacalc_row> &rows );
}

#endif
