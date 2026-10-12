#include "global.h"
#include "OnlineInputLog.h"
#include "OnlineJson.h"
#include "RageUtil.h"

static const int INPUT_LOG_VERSION = 1;

void OnlineInputLog::Clear()
{
	m_Events.clear();
	m_bHeld.clear();
	m_iColumns = 0;
	m_fRate = 1;
	m_fGlobalOffset = 0;
	m_sTiming = "{}";
	memset( &m_Settings, 0, sizeof(m_Settings) );
}

void OnlineInputLog::Begin( int iColumns, float fRate, float fGlobalOffset, const CString &sTimingJson, const OnlineLogSettings &settings )
{
	Clear();
	m_iColumns = iColumns;
	m_bHeld.resize( iColumns, false );
	m_fRate = fRate;
	m_fGlobalOffset = fGlobalOffset;
	m_sTiming = sTimingJson;
	m_Settings = settings;
	/* A song is a few thousand events; this saves the first few reallocations. */
	m_Events.reserve( 4096 );
}

void OnlineInputLog::Press( bool bMineCross, int iCol, float fMusicSeconds, float fSearchSeconds, float fBPS,
	int iRow, float fOffset, TapNoteScore tns )
{
	Event e;
	e.cType = bMineCross? 'C': 'P';
	e.bOK = false;
	e.iCol = iCol;
	e.iRow = iRow;
	e.fTime = fMusicSeconds;
	e.fSearch = fSearchSeconds;
	e.fBPS = fBPS;
	e.fOffset = iRow == -1? 0: fOffset;
	e.tns = tns;
	m_Events.push_back( e );
	if( !bMineCross && iCol >= 0 && iCol < (int) m_bHeld.size() )
		m_bHeld[iCol] = true;
}

void OnlineInputLog::Release( int iCol, float fMusicSeconds )
{
	/* A key that was already down when the song started was never pressed as
	 * far as the log knows, so its release isn't one either. */
	if( !IsHeld(iCol) )
		return;
	m_bHeld[iCol] = false;
	Event e;
	memset( &e, 0, sizeof(e) );
	e.cType = 'R';
	e.iCol = iCol;
	e.fTime = fMusicSeconds;
	m_Events.push_back( e );
}

void OnlineInputLog::Miss( int iCol, int iRow, float fFrameSeconds )
{
	Event e;
	memset( &e, 0, sizeof(e) );
	e.cType = 'X';
	e.iCol = iCol;
	e.iRow = iRow;
	e.fTime = fFrameSeconds;
	m_Events.push_back( e );
}

void OnlineInputLog::Hold( int iCol, int iRow, bool bOK, float fFrameSeconds )
{
	Event e;
	memset( &e, 0, sizeof(e) );
	e.cType = 'H';
	e.bOK = bOK;
	e.iCol = iCol;
	e.iRow = iRow;
	e.fTime = fFrameSeconds;
	m_Events.push_back( e );
}

void OnlineInputLog::End( float fFrameSeconds )
{
	Event e;
	memset( &e, 0, sizeof(e) );
	e.cType = 'E';
	e.fTime = fFrameSeconds;
	m_Events.push_back( e );
}

bool OnlineInputLog::IsHeld( int iCol ) const
{
	return iCol >= 0 && iCol < (int) m_bHeld.size() && m_bHeld[iCol];
}

const char *OnlineInputLog::TapNoteScoreName( TapNoteScore tns )
{
	switch( tns )
	{
	case TNS_HIT_MINE:	return "hit_mine";
	case TNS_MISS:		return "miss";
	case TNS_BOO:		return "boo";
	case TNS_GOOD:		return "good";
	case TNS_GREAT:		return "great";
	case TNS_PERFECT:	return "perfect";
	case TNS_MARVELOUS:	return "marvelous";
	default:			return "none";
	}
}

static CString F( float f )
{
	return JsonFloatText( f );
}

CString OnlineInputLog::ToJson() const
{
	const OnlineLogSettings &s = m_Settings;
	CString out;
	out.reserve( 256 + m_Events.size() * 48 );
	out += ssprintf( "{\"version\":%i,\"columns\":%i,\"rate\":%s,\"global_offset\":%s,\"timing\":%s,",
		INPUT_LOG_VERSION, m_iColumns, F(m_fRate).c_str(), F(m_fGlobalOffset).c_str(), m_sTiming.c_str() );
	out += ssprintf( "\"settings\":{\"judge_scale\":%s,\"judge_add\":%s,", F(s.fJudgeScale).c_str(), F(s.fJudgeAdd).c_str() );
	out += ssprintf( "\"windows\":{\"marvelous\":%s,\"perfect\":%s,\"great\":%s,\"good\":%s,\"boo\":%s,\"ok\":%s,\"mine\":%s,\"attack\":%s},",
		F(s.fMarvelous).c_str(), F(s.fPerfect).c_str(), F(s.fGreat).c_str(), F(s.fGood).c_str(),
		F(s.fBoo).c_str(), F(s.fOK).c_str(), F(s.fMine).c_str(), F(s.fAttack).c_str() );
	out += ssprintf( "\"pad_stick_seconds\":%s,\"constant_update_delta\":%s,\"show_marvelous\":%s,\"scoring_type\":\"%s\",",
		F(s.fPadStickSeconds).c_str(), F(s.fConstantUpdateDelta).c_str(), s.bShowMarvelous? "true": "false",
		s.b5thScoring? "5th": "max2" );
	out += ssprintf( "\"start_combo\":%i,\"mirror\":%s,\"autoplay\":%s,\"debug_time_keys_used\":%s},",
		s.iStartCombo, s.bMirror? "true": "false", s.bAutoplay? "true": "false", s.bDebugTimeKeysUsed? "true": "false" );

	out += "\"events\":[";
	for( unsigned i = 0; i < m_Events.size(); ++i )
	{
		const Event &e = m_Events[i];
		if( i )
			out += ',';
		switch( e.cType )
		{
		case 'P':
		case 'C':
			out += ssprintf( "[\"%c\",%i,%s,%s,%s,%i,%s,\"%s\"]", e.cType, e.iCol, F(e.fTime).c_str(), F(e.fSearch).c_str(),
				F(e.fBPS).c_str(), e.iRow, F(e.fOffset).c_str(), TapNoteScoreName(e.tns) );
			break;
		case 'R':
			out += ssprintf( "[\"R\",%i,%s]", e.iCol, F(e.fTime).c_str() );
			break;
		case 'X':
			out += ssprintf( "[\"X\",%i,%i,%s]", e.iCol, e.iRow, F(e.fTime).c_str() );
			break;
		case 'H':
			out += ssprintf( "[\"H\",%i,%i,\"%s\",%s]", e.iCol, e.iRow, e.bOK? "OK": "NG", F(e.fTime).c_str() );
			break;
		case 'E':
			out += ssprintf( "[\"E\",%s]", F(e.fTime).c_str() );
			break;
		}
	}
	out += "]}";
	return out;
}
