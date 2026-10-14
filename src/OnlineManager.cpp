#include "global.h"
#include "OnlineManager.h"
#include "ChartExport.h"
#include "ChartMSD.h"
#include "GameManager.h"
#include "GameState.h"
#include "Grade.h"
#include "HighScore.h"
#include "NoteDataWithScoring.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "StageStats.h"
#include "Steps.h"
#include "WifeManager.h"
#include "OnlineHttp.h"
#include "OnlineJson.h"
#include "song.h"

#include <algorithm>
#include <math.h>
#include <time.h>

OnlineManager *ONLINE = NULL;	// global and accessable from anywhere in our program

/* What a client of this version says it is. */
static const char *CLIENT_VERSION = "1";

static const char *GetPlatform()
{
#if defined(WINDOWS)
	return "windows";
#elif defined(DARWIN)
	return "macos";
#elif defined(FREEBSD)
	return "freebsd";
#elif defined(OPENBSD)
	return "openbsd";
#else
	return "linux";
#endif
}

static const CString VECTOR_DIR = "Data/OnlineVectors/";

OnlineManager::OnlineManager()
{
	m_bServerOK = false;
	m_InfoState = INFO_NONE;
	m_iInfoRequest = 0;
	m_fInfoRetry = 0;
	m_bQueueLoaded = false;
	FOREACH_PlayerNumber( pn )
		m_iRankRequest[pn] = 0;
	m_ChatPlayer = PLAYER_INVALID;
	m_iChatSerial = 0;
	m_iChatLastID = -1;
	m_iChatPollRequest = m_iChatSayRequest = 0;
	m_fChatNextPoll = 0;
	m_bChatOpen = false;
	m_bMusicPlaying = false;
	m_fClockSince = 0;
}

OnlineManager::~OnlineManager()
{
	OnlineHttp::Shutdown();
}

/* Online is only on with Wife scoring: scores need it, and with Wife off the
 * game must not talk to anyone, as in 3.9, even with the default server set. */
bool OnlineManager::IsEnabled() const
{
	return PREFSMAN->m_bWife && !PREFSMAN->m_sOnlineServer.empty();
}

/* The rules of section 10.1 that are known before the song starts.  The server
 * checks all of them again; this is so the player knows. */
CString OnlineManager::GetUnrankedReason( PlayerNumber pn, const NoteDataWithScoring &nd, const Steps *pSteps )
{
	if( GAMESTATE->m_PlayMode != PLAY_MODE_REGULAR || GAMESTATE->IsCourseMode() || GAMESTATE->m_bDemonstrationOrJukebox )
		return "only single songs in regular play are ranked";
	if( GAMESTATE->m_PlayerController[pn] != PC_HUMAN || PREFSMAN->m_bAutoPlay )
		return "autoplay";
	if( !ChartMSD::IsSupported(pSteps) )
		return "only 4-key dance-single charts are ranked";
	for( int t = 0; t < nd.GetNumTracks(); ++t )
		for( int r = 0; r < nd.GetNumRows(); ++r )
		{
			const TapNote::Type type = (TapNote::Type) nd.GetTapNoteX(t, r).type;
			if( type == TapNote::attack )
				return "the chart has attacks";
			if( type == TapNote::hold_tail || type == TapNote::hold )
				return "the chart has hold ends without a start";
		}

	const float fRate = floorf( GAMESTATE->m_SongOptions.m_fMusicRate * 100 + 0.5f ) / 100;
	if( fRate < 0.7f || fRate > 2.0f )
		return "the music rate must be between 0.7x and 2.0x";

	if( PREFSMAN->m_fJudgeWindowScale > 1.0f )
		return "judges easier than Judge 4 aren't ranked";
	if( PREFSMAN->m_fJudgeWindowAdd != 0 ||
		PREFSMAN->m_fJudgeWindowSecondsMarvelous != 0.0225f ||
		PREFSMAN->m_fJudgeWindowSecondsPerfect != 0.045f ||
		PREFSMAN->m_fJudgeWindowSecondsGreat != 0.090f ||
		PREFSMAN->m_fJudgeWindowSecondsGood != 0.135f ||
		PREFSMAN->m_fJudgeWindowSecondsBoo != 0.180f ||
		PREFSMAN->m_fJudgeWindowSecondsOK != 0.250f ||
		PREFSMAN->m_fJudgeWindowSecondsMine != 0.090f ||
		PREFSMAN->m_fJudgeWindowSecondsAttack != 0.135f )
		return "the judge windows aren't 3.9's";

	const PlayerOptions &po = GAMESTATE->m_PlayerOptions[pn];
	if( po.m_bTurns[PlayerOptions::TURN_LEFT] || po.m_bTurns[PlayerOptions::TURN_RIGHT] ||
		po.m_bTurns[PlayerOptions::TURN_SHUFFLE] || po.m_bTurns[PlayerOptions::TURN_SUPER_SHUFFLE] )
		return "turns other than Mirror aren't ranked";
	for( int i = 0; i < PlayerOptions::NUM_TRANSFORMS; ++i )
		if( po.m_bTransforms[i] )
			return "options that add or remove notes aren't ranked";
	if( po.m_fAppearances[PlayerOptions::APPEARANCE_RANDOMVANISH] != 0 )
		return "RandomVanish isn't ranked";

	if( GAMESTATE->m_SongOptions.m_bAutoSync )
		return "AutoSync";
	if( PREFSMAN->m_fPadStickSeconds != 0 || PREFSMAN->m_fConstantUpdateDeltaSeconds != 0 )
		return "PadStickSeconds and ConstantUpdateDeltaSeconds must be 0";
	return "";
}

void OnlineManager::Unranked( PlayerNumber pn, const CString &sWhy )
{
	PlayData &p = m_Play[pn];
	if( !p.sUnranked.empty() )
		return;
	p.sUnranked = sWhy;
	LOG->Info( "Online: P%i's play can't be ranked: %s", pn+1, sWhy.c_str() );
}

void OnlineManager::BeginSong( PlayerNumber pn, const NoteDataWithScoring &nd, const Song *pSong, const Steps *pSteps )
{
	PlayData &p = m_Play[pn];
	p = PlayData();
	m_Result[pn] = OnlineStageResult();
	m_bMusicPlaying = false;

	if( !WIFE->IsActive(pn) )
		return;
	if( !IsEnabled() && !PREFSMAN->m_bOnlineWriteTestVectors )
		return;

	p.bRecording = true;
	p.pSong = pSong;
	p.pSteps = pSteps;
	p.fRate = GAMESTATE->m_SongOptions.m_fMusicRate;
	p.fJudgeScale = PREFSMAN->m_fJudgeWindowScale;
	p.sPlayerOptions = GAMESTATE->m_PlayerOptions[pn].GetString();
	p.sSongOptions = GAMESTATE->m_SongOptions.GetString();
	p.sTimingJson = ChartExportTimingJson( pSong->m_Timing );

	OnlineLogSettings s;
	s.fJudgeScale = PREFSMAN->m_fJudgeWindowScale;
	s.fJudgeAdd = PREFSMAN->m_fJudgeWindowAdd;
	s.fMarvelous = PREFSMAN->m_fJudgeWindowSecondsMarvelous;
	s.fPerfect = PREFSMAN->m_fJudgeWindowSecondsPerfect;
	s.fGreat = PREFSMAN->m_fJudgeWindowSecondsGreat;
	s.fGood = PREFSMAN->m_fJudgeWindowSecondsGood;
	s.fBoo = PREFSMAN->m_fJudgeWindowSecondsBoo;
	s.fOK = PREFSMAN->m_fJudgeWindowSecondsOK;
	s.fMine = PREFSMAN->m_fJudgeWindowSecondsMine;
	s.fAttack = PREFSMAN->m_fJudgeWindowSecondsAttack;
	s.fPadStickSeconds = PREFSMAN->m_fPadStickSeconds;
	s.fConstantUpdateDelta = PREFSMAN->m_fConstantUpdateDeltaSeconds;
	s.bShowMarvelous = GAMESTATE->ShowMarvelous();
	s.b5thScoring = PREFSMAN->m_iScoringType == PrefsManager::SCORING_5TH;
	s.iStartCombo = g_CurStageStats.iCurCombo[pn];
	s.bMirror = GAMESTATE->m_PlayerOptions[pn].m_bTurns[PlayerOptions::TURN_MIRROR];
	s.bAutoplay = PREFSMAN->m_bAutoPlay;
	s.bDebugTimeKeysUsed = false;
	p.log.Begin( nd.GetNumTracks(), p.fRate, PREFSMAN->m_fGlobalOffsetSeconds, p.sTimingJson, s );

	const CString sWhy = GetUnrankedReason( pn, nd, pSteps );
	if( !sWhy.empty() )
		Unranked( pn, sWhy );
	LOG->Info( "Online: recording P%i's input%s", pn+1, p.sUnranked.empty()? "": " (unranked)" );

	/* Ask for a play ticket now, so it's there long before the results. */
	if( !IsEnabled() || !p.sUnranked.empty() )
		return;
	p.sChartKey = WIFE->GetReplay( pn ).sChartKey;
	if( !GetServerProblem().empty() && m_InfoState != INFO_FAILED )
		p.sTicketError = GetServerProblem();
	else if( !GetAccount(pn).bLoggedIn )
		p.sTicketError = "not logged in";
	else
		RequestTicket( pn );
}

void OnlineManager::LogStep( PlayerNumber pn, bool bMineCross, int iCol, float fMusicSeconds, float fSearchSeconds, float fBPS,
	int iRow, float fOffset, TapNoteScore tns )
{
	m_Play[pn].log.Press( bMineCross, iCol, fMusicSeconds, fSearchSeconds, fBPS, iRow, fOffset, tns );
}

void OnlineManager::LogRelease( PlayerNumber pn, int iCol, float fMusicSeconds )
{
	m_Play[pn].log.Release( iCol, fMusicSeconds );
}

void OnlineManager::LogMiss( PlayerNumber pn, int iCol, int iRow, float fFrameSeconds )
{
	m_Play[pn].log.Miss( iCol, iRow, fFrameSeconds );
}

void OnlineManager::LogHold( PlayerNumber pn, int iCol, int iRow, bool bOK, float fFrameSeconds )
{
	m_Play[pn].log.Hold( iCol, iRow, bOK, fFrameSeconds );
}

void OnlineManager::DebugTimeKeysUsed()
{
	FOREACH_PlayerNumber( pn )
	{
		if( !m_Play[pn].bRecording )
			continue;
		m_Play[pn].log.GetSettings().bDebugTimeKeysUsed = true;
		Unranked( pn, "the game's speed was changed with Tab or `" );
	}
}

/* For scripted test plays: where the music is against the system's monotonic
 * clock, once a second, so a script pressing keys can follow the song. */
void OnlineManager::LogMusicClock( float fDeltaTime )
{
#if !defined(WINDOWS)
	m_fClockSince += fDeltaTime;
	if( !m_bMusicPlaying || m_fClockSince < 1 )
		return;
	m_fClockSince = 0;
	timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	const float fMusic = GAMESTATE->m_fMusicSeconds + GAMESTATE->m_LastBeatUpdate.Ago() * GAMESTATE->m_SongOptions.m_fMusicRate;
	LOG->Info( "Online: music clock %.6f %.6f", fMusic, ts.tv_sec + ts.tv_nsec / 1e9 );
#endif
}

void OnlineManager::MusicStarted()
{
	m_bMusicPlaying = true;
	m_fClockSince = 1;
}

void OnlineManager::Update( float fDeltaTime )
{
	CheckServer();
	if( IsEnabled() && m_bServerOK && IsBuiltIn() )
	{
		UpdateAccounts();
		UpdateTickets();
		UpdateBoards();
		UpdateQueue();
		UpdateChat();
	}

	/* Things that can be switched on in the middle of a song. */
	FOREACH_PlayerNumber( pn )
	{
		PlayData &p = m_Play[pn];
		if( !p.bRecording )
			continue;
		if( PREFSMAN->m_bOnlineWriteTestVectors && pn == GAMESTATE->GetFirstHumanPlayer() )
			LogMusicClock( fDeltaTime );
		if( PREFSMAN->m_bAutoPlay || GAMESTATE->m_PlayerController[pn] != PC_HUMAN )
		{
			p.log.GetSettings().bAutoplay = true;
			Unranked( pn, "autoplay" );
		}
		if( GAMESTATE->m_SongOptions.m_bAutoSync )
			Unranked( pn, "AutoSync" );
	}
}

/* What the game decided for every note, for test vectors (section 14). */
void OnlineManager::EndSong( PlayerNumber pn, const NoteDataWithScoring &nd )
{
	PlayData &p = m_Play[pn];
	if( !p.bRecording )
		return;
	p.bRecording = false;
	p.log.End( GAMESTATE->m_fMusicSeconds );
	m_bMusicPlaying = false;

	if( ChartExportTimingJson(p.pSong->m_Timing) != p.sTimingJson )
		Unranked( pn, "the offset or BPM was changed during the song" );

	CString sNotes, sMines;
	bool bAllJudged = true;
	for( int r = 0; r < nd.GetNumRows(); ++r )
		for( int t = 0; t < nd.GetNumTracks(); ++t )
		{
			const TapNote tn = nd.GetTapNoteX( t, r );
			const TapNoteScore tns = nd.GetTapNoteScore( t, r );
			if( tn.type == TapNote::tap || tn.type == TapNote::hold_head )
			{
				if( tns == TNS_NONE )
					bAllJudged = false;
				const float fOffset = (tns == TNS_NONE || tns == TNS_MISS)? 0: nd.GetTapNoteOffset( t, r );
				if( !sNotes.empty() )
					sNotes += ',';
				sNotes += ssprintf( "[%i,%i,\"%s\",%s]", r, t, OnlineInputLog::TapNoteScoreName(tns), JsonFloatText(fOffset).c_str() );
			}
			else if( tn.type == TapNote::mine && tns == TNS_HIT_MINE )
			{
				if( !sMines.empty() )
					sMines += ',';
				sMines += ssprintf( "[%i,%i]", r, t );
			}
		}
	if( !bAllJudged )
		Unranked( pn, "some notes were never judged (the song was ended early, or a long frame skipped them)" );

	vector< pair<int,int> > vHolds;	// (row, column) -> index
	for( int i = 0; i < nd.GetNumHoldNotes(); ++i )
		vHolds.push_back( make_pair(nd.GetHoldNote(i).iStartRow * 64 + nd.GetHoldNote(i).iTrack, i) );
	sort( vHolds.begin(), vHolds.end() );
	CString sHolds;
	for( unsigned i = 0; i < vHolds.size(); ++i )
	{
		const HoldNote &hn = nd.GetHoldNote( vHolds[i].second );
		const HoldNoteScore hns = nd.GetHoldNoteScore( hn );
		if( i )
			sHolds += ',';
		sHolds += ssprintf( "[%i,%i,\"%s\"]", hn.iStartRow, hn.iTrack, hns == HNS_OK? "OK": hns == HNS_NG? "NG": "none" );
	}

	p.sGameNotes = "[" + sNotes + "]";
	p.sGameHolds = "[" + sHolds + "]";
	p.sGameMines = "[" + sMines + "]";
	LOG->Info( "Online: P%i's input log has %u events", pn+1, p.log.GetNumEvents() );
}

/* A local time as RFC 3339 in UTC. */
static CString LocalTimeToRFC3339( const DateTime &local )
{
	tm t;
	memset( &t, 0, sizeof(t) );
	t.tm_sec = local.tm_sec;
	t.tm_min = local.tm_min;
	t.tm_hour = local.tm_hour;
	t.tm_mday = local.tm_mday;
	t.tm_mon = local.tm_mon;
	t.tm_year = local.tm_year;
	t.tm_isdst = -1;
	const time_t tt = mktime( &t );
	tm utc;
#if defined(WINDOWS)
	gmtime_s( &utc, &tt );
#else
	gmtime_r( &tt, &utc );
#endif
	return ssprintf( "%04i-%02i-%02iT%02i:%02i:%02iZ", utc.tm_year+1900, utc.tm_mon+1, utc.tm_mday,
		utc.tm_hour, utc.tm_min, utc.tm_sec );
}

static JsonValue Judgments( const HighScore &hs )
{
	JsonValue j = JsonValue::Object();
	j.Set( "marvelous", JsonValue::Int(hs.iTapNoteScores[TNS_MARVELOUS]) );
	j.Set( "perfect", JsonValue::Int(hs.iTapNoteScores[TNS_PERFECT]) );
	j.Set( "great", JsonValue::Int(hs.iTapNoteScores[TNS_GREAT]) );
	j.Set( "good", JsonValue::Int(hs.iTapNoteScores[TNS_GOOD]) );
	j.Set( "boo", JsonValue::Int(hs.iTapNoteScores[TNS_BOO]) );
	j.Set( "miss", JsonValue::Int(hs.iTapNoteScores[TNS_MISS]) );
	j.Set( "hold_ok", JsonValue::Int(hs.iHoldNoteScores[HNS_OK]) );
	j.Set( "hold_ng", JsonValue::Int(hs.iHoldNoteScores[HNS_NG]) );
	j.Set( "mines_hit", JsonValue::Int(hs.iTapNoteScores[TNS_HIT_MINE]) );
	return j;
}

JsonValue OnlineManager::ClientInfo()
{
	JsonValue c = JsonValue::Object();
	c.Set( "version", JsonValue::String(CLIENT_VERSION) );
	c.Set( "platform", JsonValue::String(GetPlatform()) );
	return c;
}

/* Rates go out rounded, as "1.00": the server keeps them to two decimals too. */
JsonValue OnlineManager::RateValue( float fRate )
{
	const float fRounded = floorf( fRate * 100 + 0.5f ) / 100;
	return JsonValue::Number( fRounded, ssprintf("%.2f", fRounded) );
}

/* The body of POST /api/v1/scores (section 6.1), without the ticket if sTicket
 * is empty. */
CString OnlineManager::MakeSubmission( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs, const CString &sTicket ) const
{
	const PlayData &p = m_Play[pn];
	const WifeStageResult &wr = WIFE->GetStageResult( pn );

	JsonValue v = JsonValue::Object();
	v.Set( "score_key", JsonValue::String(wr.sScoreKey) );
	if( !sTicket.empty() )
		v.Set( "ticket", JsonValue::String(sTicket) );
	v.Set( "chart_key", JsonValue::String(wr.sChartKey) );
	v.Set( "rate", RateValue(p.fRate) );
	v.Set( "judge_scale", JsonValue::Float(p.fJudgeScale) );
	v.Set( "wife_version", JsonValue::Int(3) );
	v.Set( "wife", JsonValue::Float(wr.fWife) );
	v.Set( "judgments", Judgments(hs) );
	v.Set( "max_combo", JsonValue::Int(g_CurStageStats.iMaxCombo[pn]) );
	v.Set( "grade", JsonValue::String(GradeToOldString(hs.grade)) );
	v.Set( "played_at", JsonValue::String(LocalTimeToRFC3339(hs.dateTime)) );
	v.Set( "failed", JsonValue::Bool(false) );
	v.Set( "player_options", JsonValue::String(p.sPlayerOptions) );
	v.Set( "song_options", JsonValue::String(p.sSongOptions) );
	v.Set( "client", ClientInfo() );
	v.Set( "calc_version", JsonValue::Int(ChartMSD::GetCalcVersion()) );
	v.Set( "replay", JsonValue::String(WIFE->GetReplay(pn).ToString()) );
	v.Set( "input_log", JsonValue::Raw(p.log.ToJson()) );
	return v.Write();
}

/* "Songs/Pack/Song/" -> "Pack/Song", as the chart export names it. */
static CString PackAndSongDir( const Song *pSong )
{
	CString sDir = pSong->GetSongDir();
	sDir.Replace( '\\', '/' );
	CStringArray as;
	split( sDir, "/", as );
	if( as.size() < 2 )
		return sDir;
	return as[as.size()-2] + "/" + as[as.size()-1];
}

void OnlineManager::WriteTestVector( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs )
{
	const PlayData &p = m_Play[pn];
	const WifeStageResult &wr = WIFE->GetStageResult( pn );

	CString sGame = "{";
	sGame += "\"notes\":" + p.sGameNotes + ",";
	sGame += "\"holds\":" + p.sGameHolds + ",";
	sGame += "\"mines_hit\":" + p.sGameMines + ",";
	sGame += "\"judgments\":" + Judgments(hs).Write() + ",";
	sGame += ssprintf( "\"max_combo\":%i,", g_CurStageStats.iMaxCombo[pn] );
	sGame += "\"wife_points\":" + JsonFloatText( WIFE->GetTally(pn).GetPoints() ) + ",";
	sGame += "\"wife\":" + JsonFloatText( wr.fWife ) + ",";
	sGame += "\"wife_j4\":" + JsonFloatText( WIFE->GetTallyJ4(pn).GetFinalPercent() ) + "}";

	const CString sOut = "{\"chart\":" + ChartExportChartJson( pSong, pSteps, PackAndSongDir(pSong) ) +
		",\n\"submission\":" + MakeSubmission( pn, pSong, pSteps, hs, "" ) +
		",\n\"game\":" + sGame + "}\n";

	const CString sPath = VECTOR_DIR + wr.sScoreKey + ".json";
	RageFile f;
	if( !f.Open(sPath, RageFile::WRITE) || f.Write(sOut) == -1 || f.Flush() == -1 )
	{
		LOG->Warn( "Couldn't write the test vector \"%s\": %s", sPath.c_str(), f.GetError().c_str() );
		return;
	}
	LOG->Info( "Online: wrote the test vector %s", sPath.c_str() );
}

void OnlineManager::RecordStage( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs )
{
	OnlineStageResult &r = m_Result[pn];
	r = OnlineStageResult();
	const WifeStageResult &wr = WIFE->GetStageResult( pn );
	if( !wr.bValid )
		return;

	r.bValid = true;
	r.fRate = wr.fRate;
	r.fWifeJ4 = WIFE->GetTallyJ4( pn ).GetFinalPercent();
	if( ChartMSD::IsSupported(pSteps) )
		r.bHasSSR = ChartMSD::GetSSR( pSong, pSteps, wr.fRate, r.fWifeJ4, r.fSSR );

	const PlayData &p = m_Play[pn];
	if( p.pSong != pSong )
		return;	// not recorded

	if( PREFSMAN->m_bOnlineWriteTestVectors )
		WriteTestVector( pn, pSong, pSteps, hs );

	if( !IsEnabled() )
		return;
	r.sScoreKey = wr.sScoreKey;
	if( !p.sUnranked.empty() )
	{
		r.state = OnlineStageResult::UNRANKED;
		r.sReason = p.sUnranked;
		return;
	}

	PlayData &pd = m_Play[pn];
	pd.sBody = MakeSubmission( pn, pSong, pSteps, hs, "" );
	/* Section 12's limits; only a very long song gets near them. */
	if( pd.log.GetNumEvents() > 200000 || pd.sBody.size() > 4*1024*1024 - 4096 )
	{
		pd.sBody = "";
		r.state = OnlineStageResult::UNRANKED;
		r.sReason = "the play is too long to send";
		return;
	}
	/* The rest happens once the ticket is in. */
	pd.bWantSubmit = true;
	r.state = OnlineStageResult::WAITING;
	if( pd.iTicketRequest == 0 )
		FinishStage( pn );
}
