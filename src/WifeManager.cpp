#include "global.h"
#include "WifeManager.h"
#include "ChartKey.h"
#include "CryptManager.h"
#include "GameManager.h"
#include "GameState.h"
#include "HighScore.h"
#include "NoteData.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "Steps.h"
#include "song.h"

#include <algorithm>
#include <math.h>

WifeManager *WIFE = NULL;	// global and accessable from anywhere in our program

WifeManager::WifeManager()
{
	m_pPreload = NULL;
}

WifeManager::~WifeManager()
{
	FinishPreload();
}

/* The song's BPM changes in the form chart keys want them. */
static bool RowLess( const ChartKeyBPM &a, const ChartKeyBPM &b )
{
	return a.iRow < b.iRow;
}

CString WifeManager::GetChartKey( const Song *pSong, const Steps *pSteps )
{
	if( pSong == NULL || pSteps == NULL )
		return "";

	/* Make sure the notes are in memory (or in the cache, which loads them). */
	pSteps->Decompress();
	CString sNotes, sAttacks;
	pSteps->GetSMNoteData( sNotes, sAttacks );

	vector<ChartKeyBPM> vBPMs;
	for( unsigned i=0; i<pSong->m_Timing.m_BPMSegments.size(); i++ )
	{
		const BPMSegment &seg = pSong->m_Timing.m_BPMSegments[i];
		vBPMs.push_back( ChartKeyBPM( BeatToNoteRow(seg.m_fStartBeat), seg.m_fBPM ) );
	}
	std::stable_sort( vBPMs.begin(), vBPMs.end(), RowLess );

	return ChartKeyGenerate( sNotes, GameManager::StepsTypeToNumTracks(pSteps->m_StepsType), vBPMs );
}

/* Rates are kept to two decimals, as the options show them. */
static float RoundRate( float fRate )
{
	return floorf( fRate * 100 + 0.5f ) / 100;
}

void WifeManager::BeginSong( PlayerNumber pn, const NoteData &nd, const Song *pSong, const Steps *pSteps )
{
	PlayerData &p = m_Player[pn];
	p = PlayerData();
	m_Result[pn] = WifeStageResult();

	if( !PREFSMAN->m_bWife )
		return;
	/* Scores are for single songs that a person plays. */
	if( GAMESTATE->IsCourseMode() || GAMESTATE->m_bDemonstrationOrJukebox )
		return;
	if( GAMESTATE->m_PlayerController[pn] != PC_HUMAN )
		return;
	if( pSong == NULL || pSteps == NULL )
		return;

	/* Every tap and hold head can give the most points. */
	int iNumNotes = 0;
	if( nd.GetNumRows() > 0 )
	{
		for( int t=0; t<nd.GetNumTracks(); t++ )
		{
			const TapNote *pRows = nd.GetTrackRows( t );
			for( int r=0; r<nd.GetNumRows(); r++ )
				if( pRows[r].type == TapNote::tap || pRows[r].type == TapNote::hold_head )
					iNumNotes++;
		}
	}

	p.fJudgeScale = PREFSMAN->m_fJudgeWindowScale;
	p.fRate = RoundRate( GAMESTATE->m_SongOptions.m_fMusicRate );
	p.tally.Reset( iNumNotes * WIFE3_MAX_POINTS, p.fJudgeScale );
	p.tallyJ4.Reset( iNumNotes * WIFE3_MAX_POINTS, 1.0f );
	p.sChartKey = GetChartKey( pSong, pSteps );

	p.replay.Clear();
	p.replay.sChartKey = p.sChartKey;
	p.replay.fRate = p.fRate;
	p.replay.fSongOffset = pSong->m_Timing.m_fBeat0OffsetInSeconds;
	p.replay.fGlobalOffset = PREFSMAN->m_fGlobalOffsetSeconds;
	p.replay.sMods = GAMESTATE->m_PlayerOptions[pn].GetString();
	p.replay.iRngSeed = GAMESTATE->m_iGameSeed;

	p.bActive = true;
	p.bScored = true;
	LOG->Info( "Wife: P%i %s, %i notes, rate %.2f, judge scale %.2f", pn+1, p.sChartKey.c_str(), iNumNotes, p.fRate, p.fJudgeScale );
}

void WifeManager::Press( PlayerNumber pn, int iColumn, float fSongSeconds, int iRow, float fOffset, int iType, int iSubType )
{
	WifeInputEvent e;
	e.bPress = true;
	e.iColumn = iColumn;
	e.fSongSeconds = fSongSeconds;
	e.iNearestRow = iRow;
	e.fOffset = fOffset;
	e.iNearestType = iType;
	e.iNearestSubType = iSubType;
	m_Player[pn].replay.inputs.push_back( e );
}

void WifeManager::TapHit( PlayerNumber pn, int iColumn, int iRow, float fOffset )
{
	PlayerData &p = m_Player[pn];
	p.tally.Tap( fOffset );
	p.tallyJ4.Tap( fOffset );
	p.iHits++;
	p.fOffsetSum += fOffset;
	p.fOffsetSquares += (double) fOffset * fOffset;
	LOG->Trace( "Wife: P%i hit column %i row %i, %.4f ms", pn+1, iColumn, iRow, fOffset*1000 );
}

void WifeManager::TapMissed( PlayerNumber pn, int iColumn, int iRow, int iType, int iSubType )
{
	PlayerData &p = m_Player[pn];
	p.tally.Miss();
	p.tallyJ4.Miss();
	WifeMiss m;
	m.iColumn = iColumn;
	m.iRow = iRow;
	m.iType = iType;
	m.iSubType = iSubType;
	p.replay.misses.push_back( m );
	LOG->Trace( "Wife: P%i missed column %i row %i", pn+1, iColumn, iRow );
}

void WifeManager::MineHit( PlayerNumber pn, int iColumn, int iRow )
{
	PlayerData &p = m_Player[pn];
	p.tally.Mine();
	p.tallyJ4.Mine();
	WifeMineHit m;
	m.iRow = iRow;
	m.iColumn = iColumn;
	p.replay.mines.push_back( m );
	LOG->Trace( "Wife: P%i hit a mine in column %i row %i", pn+1, iColumn, iRow );
}

void WifeManager::HoldDropped( PlayerNumber pn, int iColumn, int iRow, int iSubType )
{
	PlayerData &p = m_Player[pn];
	if( !p.holdsCounted.insert( std::make_pair(iColumn, iRow) ).second )
		return;
	p.tally.HoldDrop();
	p.tallyJ4.HoldDrop();
	WifeHoldDrop h;
	h.iRow = iRow;
	h.iColumn = iColumn;
	h.iSubType = iSubType;
	p.replay.holds.push_back( h );
	LOG->Trace( "Wife: P%i dropped the hold in column %i row %i", pn+1, iColumn, iRow );
}

bool WifeManager::GetOffsetStats( PlayerNumber pn, float &fMeanOut, float &fSDOut ) const
{
	const PlayerData &p = m_Player[pn];
	if( p.iHits == 0 )
		return false;
	const double fMean = p.fOffsetSum / p.iHits;
	fMeanOut = (float) fMean;
	/* The sample standard deviation, as Etterna shows it. */
	fSDOut = 0;
	if( p.iHits > 1 )
	{
		const double fVariance = (p.fOffsetSquares - p.iHits * fMean * fMean) / (p.iHits - 1);
		fSDOut = (float) sqrt( max(fVariance, 0.0) );
	}
	return true;
}

CString WifeManager::GetProfileDir( PlayerNumber pn ) const
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		return PROFILEMAN->GetProfileDir( (ProfileSlot) pn );
	return PROFILEMAN->GetProfileDir( PROFILE_SLOT_MACHINE );
}

void WifeManager::GetAllProfileDirs( vector<CString> &out ) const
{
	out.push_back( PROFILEMAN->GetProfileDir( PROFILE_SLOT_MACHINE ) );

	vector<CString> vIDs;
	PROFILEMAN->GetLocalProfileIDs( vIDs );
	for( unsigned i=0; i<vIDs.size(); i++ )
		out.push_back( PROFILEMAN->GetLocalProfileDir( vIDs[i] ) );

	/* Profiles on memory cards aren't in that list. */
	FOREACH_PlayerNumber( pn )
		if( PROFILEMAN->IsUsingProfile(pn) )
		{
			const CString sDir = PROFILEMAN->GetProfileDir( (ProfileSlot) pn );
			if( std::find( out.begin(), out.end(), sDir ) == out.end() )
				out.push_back( sDir );
		}
}

void WifeManager::ScoreSet::Add( const WifeScoreEntry &e )
{
	byChart[e.sChartKey].push_back( v.size() );
	v.push_back( e );
}

int WifeManager::PreloadMain( void *p )
{
	WifeManager *pThis = (WifeManager *) p;
	for( unsigned i=0; i<pThis->m_PreloadDirs.size(); i++ )
	{
		vector<WifeScoreEntry> v;
		WifeScores::Load( pThis->m_PreloadDirs[i], v );
		for( unsigned j=0; j<v.size(); j++ )
			pThis->m_PreloadScores[i].Add( v[j] );
	}
	return 0;
}

void WifeManager::Preload()
{
	FinishPreload();

	vector<CString> vDirs;
	FOREACH_PlayerNumber( pn )
	{
		if( !GAMESTATE->IsHumanPlayer(pn) )
			continue;
		const CString sDir = GetProfileDir( pn );
		if( m_Cache.find(sDir) == m_Cache.end()  &&  std::find(vDirs.begin(), vDirs.end(), sDir) == vDirs.end() )
			vDirs.push_back( sDir );
	}
	if( vDirs.empty() )
		return;

	m_PreloadDirs = vDirs;
	m_PreloadScores.clear();
	m_PreloadScores.resize( vDirs.size() );
	m_pPreload = new RageThread;
	m_pPreload->SetName( "Wife scores" );
	m_pPreload->Create( PreloadMain, this );
}

/* Wait for the thread, and keep what it read.  Anything that adds a score has
 * to do this first: what the thread read may or may not have that score. */
void WifeManager::FinishPreload()
{
	if( m_pPreload == NULL )
		return;
	m_pPreload->Wait();
	SAFE_DELETE( m_pPreload );
	for( unsigned i=0; i<m_PreloadDirs.size(); i++ )
		m_Cache.insert( std::make_pair(m_PreloadDirs[i], m_PreloadScores[i]) );
	m_PreloadDirs.clear();
	m_PreloadScores.clear();
}

const WifeManager::ScoreSet &WifeManager::GetScores( const CString &sProfileDir )
{
	FinishPreload();
	std::map< CString, ScoreSet >::iterator it = m_Cache.find( sProfileDir );
	if( it == m_Cache.end() )
	{
		vector<WifeScoreEntry> v;
		WifeScores::Load( sProfileDir, v );
		ScoreSet set;
		for( unsigned i=0; i<v.size(); i++ )
			set.Add( v[i] );
		it = m_Cache.insert( std::make_pair(sProfileDir, set) ).first;
	}
	return it->second;
}

void WifeManager::GetLeaderboard( PlayerNumber pn, const CString &sChartKey, float fRate, bool bAllProfiles, vector<WifeScoreEntry> &out )
{
	vector<CString> vDirs;
	if( bAllProfiles )
		GetAllProfileDirs( vDirs );
	else
		vDirs.push_back( GetProfileDir(pn) );

	/* Only the chart's own scores, in the order the profiles' files hold them,
	 * go on to be picked from and sorted. */
	vector<WifeScoreEntry> chart;
	for( unsigned i=0; i<vDirs.size(); i++ )
	{
		const ScoreSet &set = GetScores( vDirs[i] );
		std::map< CString, vector<unsigned> >::const_iterator it = set.byChart.find( sChartKey );
		if( it == set.byChart.end() )
			continue;
		for( unsigned j=0; j<it->second.size(); j++ )
			chart.push_back( set.v[it->second[j]] );
	}
	WifeScores::ForChart( chart, sChartKey, fRate, out );
}

/* bAdded is whether the score made it to the file.  If not, the file is what
 * counts, as it would be if it were read again. */
void WifeManager::AddToCache( const CString &sProfileDir, const WifeScoreEntry &e, bool bAdded )
{
	std::map< CString, ScoreSet >::iterator it = m_Cache.find( sProfileDir );
	if( it == m_Cache.end() )
		return;
	WifeScoreEntry loaded;
	if( bAdded )
	{
		if( WifeScores::AsLoaded(e, sProfileDir, loaded) )
			it->second.Add( loaded );
	}
	else
		m_Cache.erase( it );
}

void WifeManager::RecordStage( PlayerNumber pn, const Song *pSong, const Steps *pSteps, HighScore &hs )
{
	FinishPreload();
	PlayerData &p = m_Player[pn];
	m_Result[pn] = WifeStageResult();
	if( !p.bScored )
		return;
	/* A score is only worth keeping if we know which chart it was on. */
	if( p.sChartKey.empty() )
		return;

	const float fWife = p.tally.GetFinalPercent();
	const CString sScoreKey = "S" + CryptManager::GetSHA1ForString(
		hs.dateTime.GetString() + p.sChartKey + ssprintf("%i", pn) + hs.sPlayerGuid );

	hs.bHasWife = true;
	hs.fWifePercent = fWife;
	hs.sWifeScoreKey = sScoreKey;

	WifeScoreEntry e;
	e.sScoreKey = sScoreKey;
	e.sChartKey = p.sChartKey;
	e.fWife = fWife;
	e.fRate = p.fRate;
	e.fJudgeScale = p.fJudgeScale;
	e.sName = PROFILEMAN->IsUsingProfile(pn)? PROFILEMAN->GetPlayerName(pn): ssprintf("P%i", pn+1);
	e.sGrade = GradeToString( hs.grade );
	e.sSong = pSong->GetFullDisplayTitle() + " - " + DifficultyToString( pSteps->GetDifficulty() ) + ssprintf(" %i", pSteps->GetMeter());
	e.dateTime = hs.dateTime;

	/* The replay goes with the player's profile, or the machine's if there isn't one. */
	p.replay.sScoreKey = sScoreKey;
	const CString sOwnerDir = GetProfileDir( pn );
	FILEMAN->CreateDir( WifeScores::GetReplayDir(sOwnerDir) );
	p.replay.Write( WifeScores::GetReplayDir(sOwnerDir) + sScoreKey );
	FlushDirCache();

	/* The scores read already get the new one, as they would read it from the file. */
	if( PROFILEMAN->IsUsingProfile(pn) )
	{
		AddToCache( sOwnerDir, e, WifeScores::Add(sOwnerDir, e) );
	}
	/* The machine keeps scores for everything but edits, like 3.9's machine high scores. */
	if( pSteps->GetDifficulty() != DIFFICULTY_EDIT )
	{
		const CString sMachineDir = PROFILEMAN->GetProfileDir( PROFILE_SLOT_MACHINE );
		AddToCache( sMachineDir, e, WifeScores::Add(sMachineDir, e) );
	}

	WifeStageResult &r = m_Result[pn];
	r.bValid = true;
	r.fWife = fWife;
	r.fRate = p.fRate;
	r.fJudgeScale = p.fJudgeScale;
	r.sChartKey = p.sChartKey;
	r.sScoreKey = sScoreKey;

	vector<WifeScoreEntry> v;
	GetLeaderboard( pn, p.sChartKey, p.fRate, false, v );
	r.iRankProfile = WifeScores::GetRank( v, sScoreKey );
	r.iCountProfile = (int) v.size();
	v.clear();
	GetLeaderboard( pn, p.sChartKey, p.fRate, true, v );
	r.iRankAll = WifeScores::GetRank( v, sScoreKey );
	r.iCountAll = (int) v.size();

	LOG->Info( "Wife: P%i scored %.4f%% (%.4f of %.2f points) on %s, rank %i of %i", pn+1, fWife*100,
		p.tally.GetPoints(), p.tally.GetTotalPoints(), p.sChartKey.c_str(), r.iRankProfile, r.iCountProfile );
}
