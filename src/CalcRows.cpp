/* Ported from Etterna (MIT license, see CalcRows.h). */
#include "global.h"
#include "CalcRows.h"
#include "NoteData.h"
#include "TimingData.h"

#include <map>
#include <math.h>

/* Etterna marks rows that only have mines and the like with this.  It's also a
 * tap in column 7, which Etterna doesn't tell apart either. */
static const unsigned NOT_A_TAP = 128;

/* Etterna's SMLoader turns BPMs above this into warps. */
static const float FAST_BPM_WARP = 9999999.f;

static int EttBeatToNoteRow( float fBeat )
{
	return (int) lroundf( fBeat * 48.F );
}

static float EttNoteRowToBeat( int iRow )
{
	return iRow / (float) ROWS_PER_BEAT;
}

/* A BPM change as Etterna keeps it: the row, and beats per second. */
struct EttBPM
{
	int iRow;
	float fBPS;
	float GetBPM() const { return fBPS * 60.0F; }
};

/* TimingData::AddSegment, for BPM changes that come in row order: a change to
 * the BPM already in force is dropped, and a second change on a row replaces
 * the first. */
static void AddBPM( vector<EttBPM> &v, int iRow, float fBPM )
{
	EttBPM seg;
	seg.iRow = iRow;
	seg.fBPS = fBPM / 60.0F;
	if( v.empty() )
	{
		v.push_back( seg );
		return;
	}

	EttBPM &cur = v.back();
	const bool bOnSameRow = cur.iRow == iRow;
	const bool bHavePrev = bOnSameRow && v.size() > 1;
	const EttBPM &prev = bHavePrev? v[v.size()-2]: cur;
	if( prev.fBPS == seg.fBPS )
	{
		if( bHavePrev )
			v.pop_back();
		return;
	}
	if( bOnSameRow )
		cur.fBPS = seg.fBPS;
	else
		v.push_back( seg );
}

bool CalcRowsTimingIsExact( const TimingData &td )
{
	if( td.m_BPMSegments.empty() )
		return false;
	/* Etterna drops stops of 0 seconds when it loads a song. */
	for( unsigned i = 0; i < td.m_StopSegments.size(); ++i )
		if( td.m_StopSegments[i].m_fStopSeconds != 0 )
			return false;
	/* Etterna's loader does odd things when nothing sets the BPM at beat 0. */
	if( td.m_BPMSegments[0].m_fStartBeat != 0 )
		return false;
	for( unsigned i = 0; i < td.m_BPMSegments.size(); ++i )
	{
		const float fBPM = td.m_BPMSegments[i].m_fBPM;
		if( !(fBPM > 0) || fBPM > FAST_BPM_WARP )
			return false;
	}
	return true;
}

/* The BPM changes as Etterna's SMLoader::ProcessBPMsAndStops makes them, for
 * timing that CalcRowsTimingIsExact accepts. */
static void GetEttBPMs( const TimingData &td, vector<EttBPM> &out )
{
	const vector<BPMSegment> &segs = td.m_BPMSegments;
	unsigned i = 0;
	float fBPM = 0;
	for( ; i < segs.size() && segs[i].m_fStartBeat <= 0; ++i )
		fBPM = segs[i].m_fBPM;
	AddBPM( out, EttBeatToNoteRow(0), fBPM );
	for( ; i < segs.size(); ++i )
		AddBPM( out, EttBeatToNoteRow(segs[i].m_fStartBeat), segs[i].m_fBPM );
}

/* TimingData::BuildAndGetEtaner for songs with only BPM changes. */
static void GetEtaner( const vector<EttBPM> &bpms, float fOffset, const vector<int> &nerv, vector<float> &out )
{
	if( bpms.size() == 1 )
	{
		const float bps = bpms[0].GetBPM() / 60.f;
		for( unsigned n = 0; n < nerv.size(); ++n )
			out.push_back( EttNoteRowToBeat(nerv[n]) / bps - fOffset );
		return;
	}

	float last_time = 0.f;
	float bps = bpms[0].GetBPM() / 60.f;
	int lastbpmrow = 0;
	size_t idx = 0;
	int event_row = 0;
	float time_to_next_event = 0.f;

	for( size_t i = 1; i < bpms.size(); ++i )
	{
		event_row = bpms[i].iRow;
		time_to_next_event = EttNoteRowToBeat( event_row - lastbpmrow ) / bps;
		const float next_event_time = last_time + time_to_next_event;
		while( idx < nerv.size() && nerv[idx] <= event_row )
		{
			const float perc = static_cast<float>(nerv[idx] - lastbpmrow) /
				static_cast<float>(event_row - lastbpmrow);
			out.push_back( last_time + time_to_next_event * perc - fOffset );
			++idx;
		}
		last_time = next_event_time;
		bps = bpms[i].GetBPM() / 60.f;
		lastbpmrow = event_row;
	}

	time_to_next_event = EttNoteRowToBeat( nerv.back() - lastbpmrow ) / bps;
	while( idx < nerv.size() )
	{
		const float perc = static_cast<float>(nerv[idx] - lastbpmrow) /
			static_cast<float>(nerv.back() - lastbpmrow);
		out.push_back( last_time + time_to_next_event * perc - fOffset );
		++idx;
	}
}

/* TimingData::GetElapsedTimeFromBeat without the global offset, which is the
 * player's and has nothing to do with the chart. */
static float GetElapsedTime( const TimingData &td, float fBeat )
{
	float fElapsedTime = -td.m_fBeat0OffsetInSeconds;

	for( unsigned j=0; j<td.m_StopSegments.size(); j++ )
	{
		if( td.m_StopSegments[j].m_fStartBeat >= fBeat )
			break;
		fElapsedTime += td.m_StopSegments[j].m_fStopSeconds;
	}

	for( unsigned i=0; i<td.m_BPMSegments.size(); i++ )
	{
		const bool bIsLastBPMSegment = i==td.m_BPMSegments.size()-1;
		const float fBPS = td.m_BPMSegments[i].m_fBPM / 60.0f;

		if( bIsLastBPMSegment )
		{
			fElapsedTime += fBeat / fBPS;
		}
		else
		{
			const float fStartBeatThisSegment = td.m_BPMSegments[i].m_fStartBeat;
			const float fStartBeatNextSegment = td.m_BPMSegments[i+1].m_fStartBeat;
			const float fBeatsInThisSegment = fStartBeatNextSegment - fStartBeatThisSegment;
			fElapsedTime += min(fBeat, fBeatsInThisSegment) / fBPS;
			fBeat -= fBeatsInThisSegment;
		}

		if( fBeat <= 0 )
			return fElapsedTime;
	}

	return fElapsedTime;
}

void CalcRowsBuild( const NoteData &nd, const TimingData &td, vector<minacalc_row> &out )
{
	out.clear();

	/* Every row with something in it, and the taps on it.  Going through the
	 * columns in order, as Etterna does, matters for the NOT_A_TAP quirks. */
	std::map<int, unsigned> rows;
	for( int t = 0; t < nd.GetNumTracks(); ++t )
	{
		for( int r = 0; r < nd.GetNumRows(); ++r )
		{
			const TapNote tn = nd.GetTapNoteX( t, r );
			if( tn.type == TapNote::empty )
				continue;
			if( tn.type == TapNote::tap || tn.type == TapNote::hold_head )
			{
				std::map<int, unsigned>::iterator it = rows.find( r );
				if( it == rows.end() )
					rows[r] = 1u << t;
				else if( it->second == NOT_A_TAP )
					it->second = 1u << t;
				else
					it->second |= 1u << t;
			}
			else
				rows.insert( std::make_pair(r, NOT_A_TAP) );
		}
	}
	if( rows.empty() )
		return;

	vector<int> nerv;
	nerv.reserve( rows.size() );
	for( std::map<int, unsigned>::const_iterator it = rows.begin(); it != rows.end(); ++it )
		nerv.push_back( it->first );

	vector<float> etaner;
	etaner.reserve( nerv.size() );
	if( CalcRowsTimingIsExact(td) )
	{
		vector<EttBPM> bpms;
		GetEttBPMs( td, bpms );
		GetEtaner( bpms, td.m_fBeat0OffsetInSeconds, nerv, etaner );
	}
	else
	{
		for( unsigned i = 0; i < nerv.size(); ++i )
			etaner.push_back( GetElapsedTime(td, NoteRowToBeat(nerv[i])) );
	}

	/* Time 0 is the first row, so the song's offset doesn't change anything. */
	size_t idx = 0;
	for( std::map<int, unsigned>::const_iterator it = rows.begin(); it != rows.end(); ++it, ++idx )
	{
		if( it->second == NOT_A_TAP )
			continue;
		minacalc_row row;
		row.notes = it->second;
		row.time = etaner[idx] - etaner[0];
		out.push_back( row );
	}
}
