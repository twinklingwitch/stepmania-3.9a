/* Ported from Etterna (MIT license, see ChartKey.h).  This follows Etterna's
 * .sm reader (NoteDataUtil::LoadFromSMNoteDataString) closely, quirks and all,
 * so the rows and notes come out the same. */
#include "global.h"
#include "ChartKey.h"
#include "CryptManager.h"
#include "RageUtil.h"

#include <map>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Etterna's TapNoteType, which is what goes into the key. */
enum
{
	CK_EMPTY,
	CK_TAP,
	CK_HOLD_HEAD,
	CK_HOLD_TAIL,
	CK_MINE,
	CK_LIFT,
	CK_AUTO_KEYSOUND,
	CK_FAKE
};

const int CK_ROWS_PER_BEAT = 48;
const int CK_MAX_NOTE_ROW = 1 << 30;

struct CKNote
{
	int type;
	int iDuration;	// hold heads only; CK_MAX_NOTE_ROW until a tail is found
};

typedef std::map<int, CKNote> CKTrack;

/* Is there a hold in this track that covers iRow?  Searching upwards from the
 * row before it, a hold head means yes unless it ended already, and anything
 * else that takes a step means no. */
static bool IsHoldNoteAtRow( const CKTrack &track, int iRow, int *pHeadRow )
{
	CKTrack::const_iterator it = track.lower_bound( iRow );
	while( it != track.begin() )
	{
		--it;
		const CKNote &tn = it->second;
		switch( tn.type )
		{
		case CK_HOLD_HEAD:
			if( tn.iDuration + it->first < iRow )
				return false;
			*pHeadRow = it->first;
			return true;
		case CK_TAP:
		case CK_MINE:
		case CK_LIFT:
		case CK_FAKE:
			return false;
		default:
			continue;	// keysounds don't count
		}
	}
	return false;
}

/* Etterna rounds the same way (lround). */
static int BeatToRow( float fBeat )
{
	return (int) lroundf( fBeat * (float) CK_ROWS_PER_BEAT );
}

static bool IsTrim( char c )
{
	return c == '\r' || c == '\n' || c == '\t' || c == ' ';
}

static void LoadNotes( const CString &sNoteDataIn, int iNumTracks, vector<CKTrack> &tracks )
{
	/* Take the comments out; the newline that ends one goes too. */
	CString sNoteData;
	{
		const CString &s = sNoteDataIn;
		size_t iCommentStart = 0, iCommentEnd = 0;
		size_t p = 0;
		while( (iCommentStart = s.find("//", iCommentEnd)) != CString::npos )
		{
			sNoteData.append( s, p, iCommentStart - iCommentEnd );
			p += iCommentStart - iCommentEnd;
			iCommentEnd = s.find( '\n', iCommentStart );
			iCommentEnd = iCommentEnd == CString::npos? s.size(): iCommentEnd + 1;
			p += iCommentEnd - iCommentStart;
		}
		sNoteData.append( s, p, s.size() - iCommentEnd );
	}

	tracks.clear();
	tracks.resize( iNumTracks );

	const size_t iSize = sNoteData.size();
	const char *pData = sNoteData.c_str();
	int m = 0;
	for( size_t iPos = 0; iPos < iSize; )
	{
		/* Measures are what's between commas; empty ones aren't measures. */
		size_t iEnd = sNoteData.find( ',', iPos );
		if( iEnd == CString::npos )
			iEnd = iSize;
		if( iEnd == iPos )
		{
			iPos = iEnd + 1;
			continue;
		}

		/* Lines are what's left in a measure after trimming, one row each. */
		vector< pair<const char *, const char *> > lines;
		size_t iLinePos = iPos;
		while( iLinePos < iEnd )
		{
			size_t iLineEnd = sNoteData.find( '\n', iLinePos );
			if( iLineEnd == CString::npos || iLineEnd > iEnd )
				iLineEnd = iEnd;
			const char *b = pData + iLinePos;
			const char *e = pData + iLineEnd;
			while( b < e && IsTrim(*b) )
				++b;
			while( e > b && IsTrim(e[-1]) )
				--e;
			if( b < e )
				lines.push_back( make_pair(b, e) );
			iLinePos = iLineEnd + 1;
		}

		for( unsigned l = 0; l < lines.size(); l++ )
		{
			const char *p = lines[l].first;
			const char *endLine = lines[l].second;

			const float fPercentIntoMeasure = l / (float) lines.size();
			const float fBeat = (m + fPercentIntoMeasure) * 4;
			const int iIndex = BeatToRow( fBeat );

			int iTrack = 0;
			while( iTrack < iNumTracks && p < endLine )
			{
				CKNote tn;
				tn.type = CK_EMPTY;
				tn.iDuration = 0;
				const char ch = *p;

				switch( ch )
				{
				case '1':
					tn.type = CK_TAP;
					break;
				case '2':
				case '4':	// rolls are hold heads too
					tn.type = CK_HOLD_HEAD;
					tn.iDuration = CK_MAX_NOTE_ROW;	// until we find the tail
					break;
				case '3':
					{
						int iHeadRow;
						if( IsHoldNoteAtRow(tracks[iTrack], iIndex, &iHeadRow) )
							tracks[iTrack][iHeadRow].iDuration = iIndex - iHeadRow;
					}
					break;
				case 'M':
					tn.type = CK_MINE;
					break;
				case 'K':
					tn.type = CK_AUTO_KEYSOUND;
					break;
				case 'L':
					tn.type = CK_LIFT;
					break;
				case 'F':
					tn.type = CK_FAKE;
					break;
				default:
					break;
				}

				p++;

				/* A keysound index in brackets can follow. */
				if( *p == '[' )
				{
					p++;
					while( p < endLine )
					{
						if( *p++ == ']' )
							break;
					}
				}

				if( tn.type != CK_EMPTY && ch != '3' )
					tracks[iTrack][iIndex] = tn;

				iTrack++;
			}
		}

		m++;
		iPos = iEnd + 1;
	}

	/* Holds that never got a tail are dropped. */
	for( int t = 0; t < iNumTracks; t++ )
	{
		CKTrack::iterator it = tracks[t].begin();
		while( it != tracks[t].end() )
		{
			CKTrack::iterator next = it;
			++next;
			if( it->second.type == CK_HOLD_HEAD && it->second.iDuration == CK_MAX_NOTE_ROW )
				tracks[t].erase( it );
			it = next;
		}
	}
}

/* The BPM in force at a row: the last change at or before it, or the first
 * change if the row comes before all of them. */
static float GetBPMAtRow( const vector<ChartKeyBPM> &vBPMs, int iRow )
{
	if( vBPMs.empty() )
		return 60;
	unsigned i = 0;
	while( i+1 < vBPMs.size() && vBPMs[i+1].iRow <= iRow )
		i++;
	return vBPMs[i].fBPM;
}

CString ChartKeyBuildString( const CString &sNoteData, int iNumTracks, const vector<ChartKeyBPM> &vBPMs )
{
	vector<CKTrack> tracks;
	LoadNotes( sNoteData, iNumTracks, tracks );

	/* Every row that has a note in some column, in order. */
	std::map<int, int> rows;
	for( int t = 0; t < iNumTracks; t++ )
		for( CKTrack::const_iterator it = tracks[t].begin(); it != tracks[t].end(); ++it )
			rows[it->first] = 0;

	CString s;
	for( std::map<int, int>::const_iterator row = rows.begin(); row != rows.end(); ++row )
	{
		for( int t = 0; t < iNumTracks; t++ )
		{
			CKTrack::const_iterator it = tracks[t].find( row->first );
			const int type = it == tracks[t].end()? CK_EMPTY: it->second.type;
			s += char('0' + type);
		}

		const float fBPM = GetBPMAtRow( vBPMs, row->first );
		s += ssprintf( "%d", (int) (fBPM + 0.374643f) );
	}
	return s;
}

CString ChartKeyGenerate( const CString &sNoteData, int iNumTracks, const vector<ChartKeyBPM> &vBPMs )
{
	return "X" + CryptManager::GetSHA1ForString( ChartKeyBuildString(sNoteData, iNumTracks, vBPMs) );
}
