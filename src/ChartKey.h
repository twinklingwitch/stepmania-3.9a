/* ChartKey - Etterna's chart keys.  A chart key names a chart by what is in it
 * rather than by where it came from, so the same chart gets the same key in
 * Etterna and here.  Used to match saved Wife scores to charts.
 *
 * Ported from Etterna's Steps::GenerateChartKey and FillStringWithBPMs (MIT
 * license, Copyright (c) 2016-2023 Etterna <etternadev@gmail.com>; the license
 * text is in Wife.h).
 *
 * The key is "X" and the SHA-1 of a string with, for every row that has any note
 * in it: one digit per column for what is in that column (Etterna's TapNoteType:
 * 0 empty, 1 tap, 2 hold head, 3 hold tail, 4 mine, 5 lift, 6 keysound, 7 fake),
 * then the BPM at that row as a whole number.  Holds only have their head in the
 * string and rows that only hold a tail don't count.
 *
 * Charts with negative BPMs or stops, which Etterna turns into warps, are not
 * handled the way Etterna does it. */

#ifndef CHART_KEY_H
#define CHART_KEY_H

struct ChartKeyBPM
{
	int iRow;
	float fBPM;
	ChartKeyBPM( int r=0, float b=0 ): iRow(r), fBPM(b) { }
};

/* sNoteData is the #NOTES data of the chart in .sm form, vBPMs the BPM changes
 * of the song sorted by row (the first one is in force until the second starts,
 * and so on). */
CString ChartKeyGenerate( const CString &sNoteData, int iNumTracks, const vector<ChartKeyBPM> &vBPMs );

/* The text that gets hashed, for tests and debugging. */
CString ChartKeyBuildString( const CString &sNoteData, int iNumTracks, const vector<ChartKeyBPM> &vBPMs );

#endif
