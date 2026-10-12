/* CalcRows - The rows MinaCalc rates a chart by, built from 3.9's NoteData and
 * TimingData the way Etterna builds them from its own (NoteData::
 * SerializeNoteData2 and TimingData::BuildAndGetEtaner).
 *
 * Every row with something in it counts for where time 0 is, but only rows with
 * a tap or hold head become calc rows: a bit per column (column 0 is bit 0) and
 * the time in seconds since the first row with anything in it.  Mines and
 * attacks only move time 0; hold ends don't count.
 *
 * The times are worked out with Etterna's own arithmetic for songs with only
 * positive BPMs and no stops (nearly all of them), so the rows come out bit for
 * bit as in Etterna.  Songs with stops use 3.9's timing, which can differ from
 * Etterna's in the last bits.  Etterna turns negative BPMs and negative stops
 * into warps and drops the notes inside them; 3.9 doesn't, so for those songs
 * the times go backwards and the calculator gives zeros.
 *
 * Ported from Etterna (MIT license, Copyright (c) 2016-2023 Etterna
 * <etternadev@gmail.com>; the license text is in Wife.h). */

#ifndef CALC_ROWS_H
#define CALC_ROWS_H

#include "minacalc_c.h"

class NoteData;
class TimingData;

void CalcRowsBuild( const NoteData &nd, const TimingData &td, vector<minacalc_row> &out );

/* Does td use Etterna's exact arithmetic (see above)? */
bool CalcRowsTimingIsExact( const TimingData &td );

#endif
