#include "global.h"
#include "ChartExport.h"
#include "BannerCache.h"
#include "CalcRows.h"
#include "ChartMSD.h"
#include "GameManager.h"
#include "NoteData.h"
#include "PrefsManager.h"
#include "ProductInfo.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SongCacheIndex.h"
#include "Steps.h"
#include "WifeManager.h"
#include "song.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

static const int FORMAT_VERSION = 1;

/* Where the pack is mounted while it's read. */
static const CString MOUNT_POINT = "/@export/";

/* A JSON string.  Text that isn't UTF-8 (old packs are often Shift-JIS) is
 * taken byte by byte as Latin-1, so the output is always valid JSON. */
static CString JsonString( const CString &s )
{
	const bool bUTF8 = utf8_is_valid( s );
	CString out = "\"";
	for( unsigned i = 0; i < s.size(); ++i )
	{
		const unsigned char c = s[i];
		switch( c )
		{
		case '"':	out += "\\\"";	break;
		case '\\':	out += "\\\\";	break;
		case '\n':	out += "\\n";	break;
		case '\r':	out += "\\r";	break;
		case '\t':	out += "\\t";	break;
		default:
			if( c < 0x20 || (c >= 0x80 && !bUTF8) )
				out += ssprintf( "\\u%04x", c );
			else
				out += (char) c;
		}
	}
	return out + "\"";
}

/* Enough digits that a float reads back as the same float. */
static CString JsonFloat( float f )
{
	if( f != f || f > FLT_MAX || f < -FLT_MAX )
		return "null";
	return ssprintf( "%.9g", f );
}

static CString Field( const char *szName, const CString &sValue )
{
	return ssprintf( "\"%s\":", szName ) + sValue;
}

/* Why a chart can't be ranked, or "" if it can. */
static CString GetUnrankedReason( const Steps *pSteps, const NoteData &nd, const vector<minacalc_row> &rows, const ChartMSDTable &msd )
{
	if( !ChartMSD::IsSupported(pSteps) )
		return "steps type";
	for( int t = 0; t < nd.GetNumTracks(); ++t )
		for( int r = 0; r < nd.GetNumRows(); ++r )
		{
			const TapNote::Type type = (TapNote::Type) nd.GetTapNoteX(t, r).type;
			if( type == TapNote::attack )
				return "attacks";
			/* A hold end without a head ('3' with no '2') stays in 3.9's notes as a
			 * note of its own, which is judged like a tap but isn't in "notes". */
			if( type == TapNote::hold_tail || type == TapNote::hold )
				return "stray hold ends";
		}
	if( rows.size() < 2 || msd.fMSD[3][MINACALC_OVERALL] <= 0 )
		return "not rated by the calc";
	return "";
}

CString ChartExportTimingJson( const TimingData &td )
{
	CString s = "{" + Field( "offset", JsonFloat(td.m_fBeat0OffsetInSeconds) ) + ",\"bpms\":[";
	for( unsigned i = 0; i < td.m_BPMSegments.size(); ++i )
		s += ssprintf( "%s[%s,%s]", i? ",": "", JsonFloat(td.m_BPMSegments[i].m_fStartBeat).c_str(), JsonFloat(td.m_BPMSegments[i].m_fBPM).c_str() );
	s += "],\"stops\":[";
	for( unsigned i = 0; i < td.m_StopSegments.size(); ++i )
		s += ssprintf( "%s[%s,%s]", i? ",": "", JsonFloat(td.m_StopSegments[i].m_fStartBeat).c_str(), JsonFloat(td.m_StopSegments[i].m_fStopSeconds).c_str() );
	return s + "]}";
}

CString ChartExportChartJson( const Song *pSong, const Steps *pSteps, const CString &sSongDir )
{
	vector<CString> f;
	f.push_back( Field("song_dir", JsonString(sSongDir)) );
	f.push_back( Field("title", JsonString(pSong->m_sMainTitle)) );
	f.push_back( Field("subtitle", JsonString(pSong->m_sSubTitle)) );
	f.push_back( Field("artist", JsonString(pSong->m_sArtist)) );
	f.push_back( Field("title_translit", JsonString(pSong->m_sMainTitleTranslit)) );
	f.push_back( Field("subtitle_translit", JsonString(pSong->m_sSubTitleTranslit)) );
	f.push_back( Field("artist_translit", JsonString(pSong->m_sArtistTranslit)) );
	f.push_back( Field("steps_type", JsonString(GameManager::StepsTypeToString(pSteps->m_StepsType))) );
	f.push_back( Field("difficulty", JsonString(DifficultyToString(pSteps->GetDifficulty()))) );
	f.push_back( Field("meter", ssprintf("%i", pSteps->GetMeter())) );
	f.push_back( Field("description", JsonString(pSteps->GetDescription())) );
	f.push_back( Field("chart_key", JsonString(WifeManager::GetChartKey(pSong, pSteps))) );
	f.push_back( Field("key_count", ssprintf("%i", GameManager::StepsTypeToNumTracks(pSteps->m_StepsType))) );

	float fMinBPM, fMaxBPM;
	pSong->m_Timing.GetActualBPM( fMinBPM, fMaxBPM );
	f.push_back( Field("bpm_min", JsonFloat(fMinBPM)) );
	f.push_back( Field("bpm_max", JsonFloat(fMaxBPM)) );
	f.push_back( Field("music_seconds", JsonFloat(pSong->m_fMusicLengthSeconds)) );

	const NoteData *pNoteData = pSteps->PeekNoteData();
	NoteData empty;
	if( pNoteData == NULL )
	{
		empty.SetNumTracks( GameManager::StepsTypeToNumTracks(pSteps->m_StepsType) );
		pNoteData = &empty;
	}
	const NoteData &nd = *pNoteData;

	vector<minacalc_row> rows;
	ChartMSDTable msd;
	memset( &msd, 0, sizeof(msd) );
	if( ChartMSD::IsSupported(pSteps) )
	{
		CalcRowsBuild( nd, pSong->m_Timing, rows );
		if( !ChartMSD::Compute(rows, ChartMSD::GetKeyCount(pSteps), msd) )
			LOG->Warn( "MinaCalc failed on %s", sSongDir.c_str() );
	}

	int iTaps = 0, iMines = 0;
	for( int t = 0; t < nd.GetNumTracks(); ++t )
		for( int r = 0; r < nd.GetNumRows(); ++r )
		{
			const TapNote tn = nd.GetTapNoteX( t, r );
			if( tn.type == TapNote::tap || tn.type == TapNote::hold_head )
				++iTaps;
			else if( tn.type == TapNote::mine )
				++iMines;
		}
	f.push_back( Field("notes_seconds", JsonFloat(rows.empty()? 0: rows.back().time)) );
	f.push_back( Field("num_taps", ssprintf("%i", iTaps)) );
	f.push_back( Field("num_holds", ssprintf("%i", nd.GetNumHoldNotes())) );
	f.push_back( Field("num_mines", ssprintf("%i", iMines)) );

	const CString sUnranked = GetUnrankedReason( pSteps, nd, rows, msd );
	f.push_back( Field("ranked", sUnranked.empty()? "true": "false") );
	f.push_back( Field("unranked_reason", sUnranked.empty()? CString("null"): JsonString(sUnranked)) );

	if( !ChartMSD::IsSupported(pSteps) )
		return "{" + join( ",", f ) + "}";

	f.push_back( Field("rows_exact", CalcRowsTimingIsExact(pSong->m_Timing)? "true": "false") );

	CString s = "[";
	for( unsigned i = 0; i < rows.size(); ++i )
		s += ssprintf( "%s[%s,%u]", i? ",": "", JsonFloat(rows[i].time).c_str(), rows[i].notes );
	f.push_back( Field("rows", s + "]") );

	s = "[";
	for( int r = 0; r < MINACALC_NUM_RATES; ++r )
	{
		s += r? ",[": "[";
		for( int ss = 0; ss < MINACALC_NUM_SKILLSETS; ++ss )
			s += (ss? ",": "") + JsonFloat( msd.fMSD[r][ss] );
		s += "]";
	}
	f.push_back( Field("msd", s + "]") );

	/* The song's timing and every note, for judging replays the way the game does. */
	f.push_back( Field("timing", ChartExportTimingJson(pSong->m_Timing)) );

	vector<int> viHoldEnd( nd.GetNumTracks() * max(nd.GetNumRows(), 1), -1 );
	for( int i = 0; i < nd.GetNumHoldNotes(); ++i )
	{
		const HoldNote &hn = nd.GetHoldNote( i );
		if( hn.iStartRow < nd.GetNumRows() )
			viHoldEnd[hn.iTrack * nd.GetNumRows() + hn.iStartRow] = hn.iEndRow;
	}
	s = "[";
	bool bFirst = true;
	for( int r = 0; r < nd.GetNumRows(); ++r )
		for( int t = 0; t < nd.GetNumTracks(); ++t )
		{
			const TapNote tn = nd.GetTapNoteX( t, r );
			CString sNote;
			switch( tn.type )
			{
			case TapNote::tap:	sNote = ssprintf( "[%i,%i,1]", r, t );	break;
			case TapNote::hold_head:	sNote = ssprintf( "[%i,%i,2,%i]", r, t, viHoldEnd[t * nd.GetNumRows() + r] );	break;
			case TapNote::mine:	sNote = ssprintf( "[%i,%i,4]", r, t );	break;
			default:	continue;
			}
			s += (bFirst? "": ",") + sNote;
			bFirst = false;
		}
	f.push_back( Field("notes", s + "]") );

	return "{" + join( ",", f ) + "}";
}

int ChartExportMain( const CString &sPackDirIn, const CString &sOutFile )
{
	CString sPackDir = sPackDirIn;
	while( sPackDir.size() > 1 && (sPackDir.Right(1) == "/" || sPackDir.Right(1) == "\\") )
		sPackDir.erase( sPackDir.size() - 1 );
	CString sPack = sPackDir;
	sPack.Replace( '\\', '/' );
	sPack = Basename( sPack );
	if( sPack.empty() || sOutFile.empty() )
	{
		fprintf( stderr, "Usage: stepmania --export-charts=<pack directory> --export-out=<file.json>\n" );
		return 2;
	}

	/* Load songs the way the game does, minus everything that would write to
	 * the user's files: no song cache, no banner cache, no autogen. */
	PREFSMAN->m_BannerCache = PrefsManager::BNCACHE_OFF;
	PREFSMAN->m_bAutogenSteps = false;
	GAMEMAN = new GameManager;
	SONGINDEX = new SongCacheIndex( false );
	BANNERCACHE = new BannerCache;

	const CString sMountDir = MOUNT_POINT + sPack + "/";
	FILEMAN->Mount( "dir", sPackDir, sMountDir );
	if( !IsADirectory(MOUNT_POINT + sPack) )
	{
		fprintf( stderr, "\"%s\" isn't a directory\n", sPackDir.c_str() );
		return 1;
	}

	CStringArray asSongDirs;
	GetDirListing( sMountDir + "*", asSongDirs, true );
	SortCStringArray( asSongDirs );

	vector<CString> vsCharts, vsSkipped;
	for( unsigned i = 0; i < asSongDirs.size(); ++i )
	{
		if( !stricmp(asSongDirs[i], "cvs") )
			continue;
		const CString sSongDir = sPack + "/" + asSongDirs[i];
		Song *pSong = new Song;
		if( !pSong->LoadFromSongDir(sMountDir + asSongDirs[i]) )
		{
			vsSkipped.push_back( "{" + Field("song_dir", JsonString(sSongDir)) + "," +
				Field("reason", JsonString("not a song the game would load")) + "}" );
			delete pSong;
			continue;
		}

		const vector<Steps*> &vpSteps = pSong->GetAllSteps();
		for( unsigned s = 0; s < vpSteps.size(); ++s )
		{
			if( vpSteps[s]->IsAutogen() )
				continue;
			vsCharts.push_back( ChartExportChartJson(pSong, vpSteps[s], sSongDir) );
			vpSteps[s]->Compress();
		}
		LOG->Info( "Exported %s", sSongDir.c_str() );
		delete pSong;
	}

	CString sRates = "[";
	for( int r = 0; r < MINACALC_NUM_RATES; ++r )
		sRates += ssprintf( "%s%.1f", r? ",": "", (r + 7) / 10.0f );
	sRates += "]";
	CString sSkillsets = "[";
	for( int ss = 0; ss < MINACALC_NUM_SKILLSETS; ++ss )
		sSkillsets += (ss? ",": "") + JsonString( ChartMSD::SkillsetName(ss) );
	sSkillsets += "]";

	CString sOut = "{";
	sOut += Field( "format", JsonString("stepmania39-charts") ) + ",\n";
	sOut += Field( "format_version", ssprintf("%i", FORMAT_VERSION) ) + ",\n";
	sOut += Field( "game_version", JsonString(PRODUCT_NAME_VER) ) + ",\n";
	sOut += Field( "calc_version", ssprintf("%i", ChartMSD::GetCalcVersion()) ) + ",\n";
	sOut += Field( "rates", sRates ) + ",\n";
	sOut += Field( "skillsets", sSkillsets ) + ",\n";
	sOut += Field( "pack", JsonString(sPack) ) + ",\n";
	sOut += "\"charts\":[\n" + join( ",\n", vsCharts ) + "\n],\n";
	sOut += "\"skipped\":[\n" + join( ",\n", vsSkipped ) + "\n]}\n";

	/* A path outside the game's directories, so not through RageFile. */
	FILE *fp = fopen( sOutFile, "wb" );
	if( fp == NULL || fwrite(sOut.data(), 1, sOut.size(), fp) != sOut.size() || fclose(fp) != 0 )
	{
		fprintf( stderr, "Couldn't write \"%s\"\n", sOutFile.c_str() );
		return 1;
	}

	LOG->Info( "Exported %u charts of %u songs from \"%s\" to \"%s\"",
		(unsigned) vsCharts.size(), (unsigned) (asSongDirs.size() - vsSkipped.size()), sPackDir.c_str(), sOutFile.c_str() );
	printf( "%u charts, %u songs skipped\n", (unsigned) vsCharts.size(), (unsigned) vsSkipped.size() );
	return 0;
}
