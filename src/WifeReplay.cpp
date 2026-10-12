/* Ported from Etterna (MIT license, see WifeReplay.h). */
#include "global.h"
#include "WifeReplay.h"
#include "Wife.h"
#include "RageFile.h"
#include "RageUtil.h"
#include "RageLog.h"

#include <math.h>
#include <stdlib.h>

static const int INPUT_DATA_VERSION = 3;

void WifeReplay::Clear()
{
	sChartKey = "";
	sScoreKey = "";
	fRate = 1;
	fSongOffset = 0;
	fGlobalOffset = 0;
	sMods = "";
	iRngSeed = 0;
	inputs.clear();
	holds.clear();
	mines.clear();
	misses.clear();
}

CString WifeReplay::ToString() const
{
	/* The header is split on spaces when it is read, so the mods can't have any. */
	CString sModStr = sMods;
	sModStr.Replace( " ", "" );
	if( sModStr.empty() )
		sModStr = "none";

	CString s = ssprintf( "%s %s %f %f %f %s %i %i\n",
		sChartKey.c_str(), sScoreKey.c_str(), fRate, fSongOffset, fGlobalOffset,
		sModStr.c_str(), iRngSeed, INPUT_DATA_VERSION );

	for( unsigned i=0; i<inputs.size(); i++ )
	{
		const WifeInputEvent &e = inputs[i];
		s += ssprintf( "%i %i %f %i %f %i %i\n", e.iColumn, e.bPress? 1: 0, e.fSongSeconds,
			e.iNearestRow, e.fOffset, e.iNearestType, e.iNearestSubType );
	}

	for( unsigned i=0; i<holds.size(); i++ )
	{
		const WifeHoldDrop &h = holds[i];
		s += ssprintf( "H %i %i", h.iRow, h.iColumn );
		if( h.iSubType != WIFE_SUBTYPE_HOLD )
			s += ssprintf( " %i", h.iSubType );
		s += "\n";
	}

	for( unsigned i=0; i<mines.size(); i++ )
		s += ssprintf( "M %i %i\n", mines[i].iRow, mines[i].iColumn );

	for( unsigned i=0; i<misses.size(); i++ )
		s += ssprintf( "X %i %i %i %i\n", misses[i].iColumn, misses[i].iRow, misses[i].iType, misses[i].iSubType );

	return s;
}

bool WifeReplay::FromString( const CString &sIn )
{
	Clear();

	CStringArray asLines;
	split( sIn, "\n", asLines );
	if( asLines.empty() )
		return false;

	{
		CStringArray as;
		split( asLines[0], " ", as );
		if( as.size() != 8 )
			return false;
		if( atoi(as[7]) != INPUT_DATA_VERSION )
			return false;
		sChartKey = as[0];
		sScoreKey = as[1];
		fRate = strtof( as[2], NULL );
		fSongOffset = strtof( as[3], NULL );
		fGlobalOffset = strtof( as[4], NULL );
		sMods = as[5];
		iRngSeed = atoi( as[6] );
	}

	for( unsigned i=1; i<asLines.size(); i++ )
	{
		CString sLine = asLines[i];
		TrimRight( sLine );
		CStringArray as;
		split( sLine, " ", as );
		if( as.empty() )
			continue;

		if( as[0] == "H" )
		{
			if( as.size() < 3 )
				return false;
			WifeHoldDrop h;
			h.iRow = atoi( as[1] );
			h.iColumn = atoi( as[2] );
			h.iSubType = as.size() > 3? atoi( as[3] ): WIFE_SUBTYPE_HOLD;
			holds.push_back( h );
		}
		else if( as[0] == "M" )
		{
			if( as.size() < 3 )
				return false;
			WifeMineHit m;
			m.iRow = atoi( as[1] );
			m.iColumn = atoi( as[2] );
			mines.push_back( m );
		}
		else if( as[0] == "X" )
		{
			if( as.size() < 5 )
				return false;
			WifeMiss x;
			/* these were backwards in Etterna at first, and stayed that way */
			x.iColumn = atoi( as[1] );
			x.iRow = atoi( as[2] );
			x.iType = atoi( as[3] );
			x.iSubType = atoi( as[4] );
			misses.push_back( x );
		}
		else
		{
			if( as.size() != 7 )
				return false;
			WifeInputEvent e;
			e.iColumn = atoi( as[0] );
			e.bPress = atoi( as[1] ) != 0;
			e.fSongSeconds = strtof( as[2], NULL );
			e.iNearestRow = atoi( as[3] );
			e.fOffset = strtof( as[4], NULL );
			e.iNearestType = atoi( as[5] );
			e.iNearestSubType = atoi( as[6] );
			inputs.push_back( e );
		}
	}
	return true;
}

bool WifeReplay::Write( const CString &sPath ) const
{
	RageFile f;
	if( !f.Open( sPath, RageFile::WRITE ) )
	{
		LOG->Warn( "Couldn't write replay \"%s\": %s", sPath.c_str(), f.GetError().c_str() );
		return false;
	}
	const CString s = ToString();
	if( f.Write( s ) == -1 || f.Flush() == -1 )
	{
		LOG->Warn( "Error writing replay \"%s\": %s", sPath.c_str(), f.GetError().c_str() );
		return false;
	}
	return true;
}

bool WifeReplay::Read( const CString &sPath )
{
	RageFile f;
	if( !f.Open( sPath, RageFile::READ ) )
		return false;
	CString s;
	if( f.Read( s, f.GetFileSize() ) == -1 )
		return false;
	return FromString( s );
}

/* The boo window of Judge 4 in 3.9, which scales with the judge. */
static const float WIFE_BOO_WINDOW = 0.180f;

static bool IsHit( const WifeInputEvent &e, float fTimingScale )
{
	if( !e.bPress )
		return false;
	if( e.iNearestType != WIFE_TYPE_TAP && e.iNearestType != WIFE_TYPE_HOLD_HEAD )
		return false;
	return fabsf( e.fOffset ) <= WIFE_BOO_WINDOW * fTimingScale;
}

float WifeReplay::GetPoints( float fTimingScale ) const
{
	float fPoints = 0;
	for( unsigned i=0; i<inputs.size(); i++ )
		if( IsHit( inputs[i], fTimingScale ) )
			fPoints += Wife3( inputs[i].fOffset, fTimingScale );
	fPoints += misses.size() * WIFE3_MISS_WEIGHT;
	fPoints += mines.size() * WIFE3_MINE_HIT_WEIGHT;
	fPoints += holds.size() * WIFE3_HOLD_DROP_WEIGHT;
	return fPoints;
}

int WifeReplay::GetNumNotes( float fTimingScale ) const
{
	int n = (int) misses.size();
	for( unsigned i=0; i<inputs.size(); i++ )
		if( IsHit( inputs[i], fTimingScale ) )
			n++;
	return n;
}
