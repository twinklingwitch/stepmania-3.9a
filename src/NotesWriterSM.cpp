#include "global.h"
#include "NotesWriterSM.h"
#include "Steps.h"
#include "RageUtil.h"
#include "GameManager.h"
#include "RageLog.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "song.h"
#include "MsdFile.h"
#include <cstring>
#include <cerrno>

/* The cache keeps the timing exactly as it was read: online scores send it, and
 * the server compares it with the simfile's.  Rounding it to three decimals would
 * make a song loaded from the cache look like it had been resynced. */
static CString TimingValue( float f, bool bSavingCache )
{
	return ssprintf( bSavingCache? "%.9g": "%.3f", f );
}

void NotesWriterSM::WriteGlobalTags( RageFile &f, const Song &out, bool bSavingCache )
{
	f.PutLine( ssprintf( "#TITLE:%s;", out.m_sMainTitle.c_str() ) );
	f.PutLine( ssprintf( "#SUBTITLE:%s;", out.m_sSubTitle.c_str() ) );
	f.PutLine( ssprintf( "#ARTIST:%s;", out.m_sArtist.c_str() ) );
	f.PutLine( ssprintf( "#TITLETRANSLIT:%s;", out.m_sMainTitleTranslit.c_str() ) );
	f.PutLine( ssprintf( "#SUBTITLETRANSLIT:%s;", out.m_sSubTitleTranslit.c_str() ) );
	f.PutLine( ssprintf( "#ARTISTTRANSLIT:%s;", out.m_sArtistTranslit.c_str() ) );
	f.PutLine( ssprintf( "#CREDIT:%s;", out.m_sCredit.c_str() ) );
	f.PutLine( ssprintf( "#BANNER:%s;", out.m_sBannerFile.c_str() ) );
	f.PutLine( ssprintf( "#BACKGROUND:%s;", out.m_sBackgroundFile.c_str() ) );
	f.PutLine( ssprintf( "#LYRICSPATH:%s;", out.m_sLyricsFile.c_str() ) );
	f.PutLine( ssprintf( "#CDTITLE:%s;", out.m_sCDTitleFile.c_str() ) );
	f.PutLine( ssprintf( "#MUSIC:%s;", out.m_sMusicFile.c_str() ) );
	f.PutLine( ssprintf( "#OFFSET:%s;", TimingValue(out.m_Timing.m_fBeat0OffsetInSeconds, bSavingCache).c_str() ) );
	f.PutLine( ssprintf( "#SAMPLESTART:%.3f;", out.m_fMusicSampleStartSeconds ) );
	f.PutLine( ssprintf( "#SAMPLELENGTH:%.3f;", out.m_fMusicSampleLengthSeconds ) );

	f.Write( "#SELECTABLE:" );
	switch(out.m_SelectionDisplay) {
	default: ASSERT(0);  /* fallthrough */
	case Song::SHOW_ALWAYS:
		f.Write( "YES" ); break;
	case Song::SHOW_NEVER:
		f.Write( "NO" ); break;
	case Song::SHOW_ROULETTE:
		f.Write( "ROULETTE" ); break;
	}
	f.PutLine( ";" );

	switch( out.m_DisplayBPMType )
	{
	case Song::DISPLAY_ACTUAL:
		// write nothing
		break;
	case Song::DISPLAY_SPECIFIED:
		if( out.m_fSpecifiedBPMMin == out.m_fSpecifiedBPMMax )
			f.PutLine( ssprintf( "#DISPLAYBPM:%.3f;", out.m_fSpecifiedBPMMin ) );
		else
			f.PutLine( ssprintf( "#DISPLAYBPM:%.3f:%.3f;", out.m_fSpecifiedBPMMin, out.m_fSpecifiedBPMMax ) );
		break;
	case Song::DISPLAY_RANDOM:
		f.PutLine( ssprintf( "#DISPLAYBPM:*;" ) );
		break;
	}


	f.Write( "#BPMS:" );
	unsigned i;
	for( i=0; i<out.m_Timing.m_BPMSegments.size(); i++ )
	{
		const BPMSegment &bs = out.m_Timing.m_BPMSegments[i];

		f.Write( TimingValue(bs.m_fStartBeat, bSavingCache) + "=" + TimingValue(bs.m_fBPM, bSavingCache) );
		if( i != out.m_Timing.m_BPMSegments.size()-1 )
			f.Write( "," );
	}
	f.PutLine( ";" );

	f.Write( "#STOPS:" );
	for( i=0; i<out.m_Timing.m_StopSegments.size(); i++ )
	{
		const StopSegment &fs = out.m_Timing.m_StopSegments[i];

		f.PutLine( TimingValue(fs.m_fStartBeat, bSavingCache) + "=" + TimingValue(fs.m_fStopSeconds, bSavingCache) );
		if( i != out.m_Timing.m_StopSegments.size()-1 )
			f.Write( "," );
	}
	f.PutLine( ";" );
	
	f.Write( "#BGCHANGES:" );
	for( i=0; i<out.m_BackgroundChanges.size(); i++ )
	{
		const BackgroundChange &seg = out.m_BackgroundChanges[i];

		f.PutLine( ssprintf( "%.3f=%s=%.3f=%d=%d=%d,", seg.m_fStartBeat, seg.m_sBGName.c_str(), seg.m_fRate, seg.m_bFadeLast, seg.m_bRewindMovie, seg.m_bLoop ) );
	}
	/* If there's an animation plan at all, add a dummy "-nosongbg-" tag to indicate that
	 * this file doesn't want a song BG entry added at the end.  See SMLoader::TidyUpData.
	 * This tag will be removed on load.  Add it at a very high beat, so it won't cause
	 * problems if loaded in older versions. */
	if( !out.m_BackgroundChanges.empty() )
		f.PutLine( "99999=-nosongbg-=1.000=0=0=0 // don't automatically add -songbackground-" );
	f.PutLine( ";" );

	if( out.m_ForegroundChanges.size() )
	{
		f.Write( "#FGCHANGES:" );
		for( i=0; i<out.m_ForegroundChanges.size(); i++ )
		{
			const BackgroundChange &seg = out.m_ForegroundChanges[i];

			f.PutLine( ssprintf( "%.3f=%s=%.3f=%d=%d=%d", seg.m_fStartBeat, seg.m_sBGName.c_str(), seg.m_fRate, seg.m_bFadeLast, seg.m_bRewindMovie, seg.m_bLoop ) );
			if( i != out.m_ForegroundChanges.size()-1 )
				f.Write( "," );
		}
		f.PutLine( ";" );
	}
}

static void WriteLineList( RageFile &f, vector<CString> &lines, bool SkipLeadingBlankLines, bool OmitLastNewline )
{
	for( unsigned i = 0; i < lines.size(); ++i )
	{
		TrimRight( lines[i] );
		if( SkipLeadingBlankLines )
		{
			if( lines.size() == 0 )
				continue;
			SkipLeadingBlankLines = false;
		}
		f.Write( lines[i] );

		if( !OmitLastNewline || i+1 < lines.size() )
			f.PutLine( "" ); /* newline */
	}
}

void NotesWriterSM::WriteSMNotesTag( const Steps &in, RageFile &f, bool bSavingCache, const unsigned *puHash )
{
	f.PutLine( "" );
	f.PutLine( ssprintf( "//---------------%s - %s----------------",
		GameManager::StepsTypeToString(in.m_StepsType).c_str(), in.GetDescription().c_str() ) );
	f.PutLine( "#NOTES:" );
	f.PutLine( ssprintf( "     %s:", GameManager::StepsTypeToString(in.m_StepsType).c_str() ) );
	f.PutLine( ssprintf( "     %s:", in.GetDescription().c_str() ) );
	f.PutLine( ssprintf( "     %s:", DifficultyToString(in.GetDifficulty()).c_str() ) );
	f.PutLine( ssprintf( "     %d:", in.GetMeter() ) );
	
	int MaxRadar = bSavingCache? NUM_RADAR_CATEGORIES:5;
	CStringArray asRadarValues;
	for( int r=0; r < MaxRadar; r++ )
		asRadarValues.push_back( ssprintf("%.3f", in.GetRadarValues()[r]) );
	/* Don't append a newline here; it's added in NoteDataUtil::GetSMNoteDataString.
	 * If we add it here, then every time we write unmodified data we'll add an extra
	 * newline and they'll accumulate. */
	f.Write( ssprintf( "     %s:", join(",",asRadarValues).c_str() ) );

	if( puHash != NULL )
	{
		/* No note data or attacks; instead, the hash the loader would have made of them. */
		f.PutLine( ssprintf( "::%u;", *puHash ) );
		return;
	}

	CString sNoteData;
	CString sAttackData;
	in.GetSMNoteData( sNoteData, sAttackData );

	vector<CString> lines;

	split( sNoteData, "\n", lines, false );
	WriteLineList( f, lines, true, true );

	if( sAttackData.empty() )
		f.PutLine( ";" );
	else
	{
		f.PutLine( ":" );

		lines.clear();
		split( sAttackData, "\n", lines, false );
		WriteLineList( f, lines, true, true );

		f.PutLine( ";" );
	}
}

bool NotesWriterSM::Write(CString sPath, const Song &out, bool bSavingCache)
{
	/* Flush dir cache when writing steps, so the old size isn't cached. */
	FILEMAN->FlushDirCache( Dirname(sPath) );

	int flags = RageFile::WRITE;

	/* If we're not saving cache, we're saving real data, so enable SLOW_FLUSH
	 * to prevent data loss.  If we're saving cache, this will slow things down
	 * too much. */
	if( !bSavingCache )
		flags |= RageFile::SLOW_FLUSH;

	RageFile f;
	if( !f.Open( sPath, flags ) )
	{
		LOG->Warn( "Error opening song file '%s' for writing: %s", sPath.c_str(), f.GetError().c_str() );
		return false;
	}

	WriteSong( f, out, bSavingCache, NULL );
	return true;
}

/* Write the cache version of a song into a string. */
bool NotesWriterSM::WriteToString( CString &sOut, const Song &out, const vector<unsigned> *pHashes )
{
	const CString sPath = ssprintf( "@mem/NotesWriterSM%p", this );

	RageFile f;
	if( !f.Open( sPath, RageFile::WRITE ) )
	{
		LOG->Warn( "Error opening '%s' for writing: %s", sPath.c_str(), f.GetError().c_str() );
		return false;
	}
	const bool bWritten = WriteSong( f, out, true, pHashes );
	f.Close();

	sOut = "";
	RageFile r;
	bool bRet = bWritten && r.Open( sPath, RageFile::READ ) && r.Read( sOut ) != -1;
	r.Close();
	FILEMAN->Remove( sPath );
	return bRet;
}

/*
 * Write the cache data for a song: sDataOut is the whole song, sMetaOut is the same
 * but without the note data, which is much quicker to load.  Steps::GetHash depends
 * on the note data, so the hashes go into sMetaOut instead.  Take them from what the
 * loader makes of sDataOut, since that isn't always the same as what was in memory.
 */
bool NotesWriterSM::WriteCache( CString &sDataOut, CString &sMetaOut, const Song &out )
{
	if( !WriteToString( sDataOut, out, NULL ) )
		return false;

	MsdFile msd;
	msd.ReadFromString( sDataOut );

	vector<unsigned> aHashes;
	for( unsigned i = 0; i < msd.GetNumValues(); ++i )
	{
		if( stricmp(msd.GetParam(i, 0), "NOTES") || msd.GetNumParams(i) < 7 )
			continue;
		aHashes.push_back( GetHashForString(msd.GetParam(i, 6)) );
	}

	return WriteToString( sMetaOut, out, &aHashes );
}

bool NotesWriterSM::WriteSong( RageFile &f, const Song &out, bool bSavingCache, const vector<unsigned> *pHashes )
{
	unsigned i;

	WriteGlobalTags( f, out, bSavingCache );
	if( bSavingCache )
	{
		f.PutLine( ssprintf( "// cache tags:" ) );
		f.PutLine( ssprintf( "#FIRSTBEAT:%.3f;", out.m_fFirstBeat ) );
		f.PutLine( ssprintf( "#LASTBEAT:%.3f;", out.m_fLastBeat ) );
		f.PutLine( ssprintf( "#SONGFILENAME:%s;", out.m_sSongFileName.c_str() ) );
		f.PutLine( ssprintf( "#HASMUSIC:%i;", out.m_bHasMusic ) );
		f.PutLine( ssprintf( "#HASBANNER:%i;", out.m_bHasBanner ) );
		f.PutLine( ssprintf( "#MUSICLENGTH:%.3f;", out.m_fMusicLengthSeconds ) );
		f.PutLine( ssprintf( "// end cache tags" ) );
	}

	//
	// Save all Steps for this file
	//
	unsigned iWritten = 0;
	const vector<Steps*>& vpSteps = out.GetAllSteps();
	for( i=0; i<vpSteps.size(); i++ ) 
	{
		const Steps* pSteps = vpSteps[i];
		if( pSteps->IsAutogen() )
			continue; /* don't write autogen notes */

		/* Only save steps that weren't loaded from a profile. */
		if( pSteps->WasLoadedFromProfile() )
			continue;

		const unsigned *puHash = NULL;
		if( pHashes != NULL )
		{
			if( iWritten >= pHashes->size() )
				return false;
			puHash = &(*pHashes)[iWritten];
		}
		WriteSMNotesTag( *pSteps, f, bSavingCache, puHash );
		++iWritten;
	}

	return pHashes == NULL || iWritten == pHashes->size();
}

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
