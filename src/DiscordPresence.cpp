#include "global.h"
#include "DiscordPresence.h"
#include "DiscordIpc.h"
#if !defined(WITHOUT_EXTRAS)
#include "ChartMSD.h"
#endif
#include "Course.h"
#include "GameState.h"
#include "OnlineJson.h"
#if !defined(WITHOUT_EXTRAS)
#include "OnlineManager.h"
#endif
#include "PrefsManager.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "ScreenAttract.h"
#include "ScreenEdit.h"
#include "ScreenEditMenu.h"
#include "ScreenEvaluation.h"
#include "ScreenEz2SelectMusic.h"
#include "ScreenGameplay.h"
#include "ScreenJukebox.h"
#include "ScreenManager.h"
#include "ScreenNetSelectMusic.h"
#include "ScreenPlayerOptions.h"
#include "ScreenSelectMusic.h"
#include "ScreenSongOptions.h"
#include "ScreenStage.h"
#include "StageStats.h"
#include "Steps.h"
#include "Trail.h"
#if !defined(WITHOUT_EXTRAS)
#include "WifeManager.h"
#endif
#include "song.h"

#include <math.h>

DiscordPresence *DISCORD = NULL;

static const int64_t RETRY_MS = 15000;			// look for Discord again
static const int64_t RECONNECT_MS = 1000;		// after losing a connection that worked
static const int64_t MIN_UPDATE_MS = 2000;		// between activity updates
static const int64_t READY_TIMEOUT_MS = 10000;	// for Discord to answer the handshake
static const float LOOK_SECONDS = 0.25f;		// how often the game is looked at

/* What the game and the thread share. */
struct DiscordShared
{
	RageMutex mutex;

	/* From the game. */
	bool bQuit;
	bool bEnabled;
	CString sAppId;
	CString sActivity;		// JSON

#if !defined(WITHOUT_EXTRAS)
	/* An MSD to work out: the whole table, or at one rate if fRate isn't 0. */
	int iJob;				// counts up; only the latest is wanted
	vector<minacalc_row> rows;
	unsigned iKeyCount;
	float fRate;

	/* Its answer. */
	int iJobDone;
	bool bJobOK;
	ChartMSDTable table;
	float fAtRate[MINACALC_NUM_SKILLSETS];
#endif

	/* Posted to wake the thread when the game quits, and by the thread when it's done. */
	RageSemaphore wake;
	RageSemaphore exited;

#if !defined(WITHOUT_EXTRAS)
	DiscordShared(): mutex( "Discord" ), bQuit( false ), bEnabled( false ), iJob( 0 ), iKeyCount( 4 ), fRate( 0 ),
		iJobDone( 0 ), bJobOK( false ), wake( "DiscordWake" ), exited( "DiscordExited" ) { }
#else
	DiscordShared(): mutex( "Discord" ), bQuit( false ), bEnabled( false ), wake( "DiscordWake" ), exited( "DiscordExited" ) { }
#endif
};

/*
 * The thread.
 */

static CString EndpointName( const CString &sEndpoint )
{
	const size_t i = sEndpoint.find_last_of( "/\\" );
	return i == CString::npos? sEndpoint: CString( sEndpoint.substr(i + 1) );
}

namespace
{
	class Worker
	{
	public:
#if !defined(WITHOUT_EXTRAS)
		Worker( DiscordShared &sh ): m_Shared( sh ), m_State( CLOSED ), m_iNextTry( 0 ), m_iLastSend( 0 ),
			m_iReadyDeadline( 0 ), m_bSentAny( false ), m_iNonce( 0 ), m_iPid( DiscordIpc::GetPid() ), m_pCalc( NULL ) { }
		~Worker() { if( m_pCalc != NULL ) minacalc_destroy( m_pCalc ); }
#else
		Worker( DiscordShared &sh ): m_Shared( sh ), m_State( CLOSED ), m_iNextTry( 0 ), m_iLastSend( 0 ),
			m_iReadyDeadline( 0 ), m_bSentAny( false ), m_iNonce( 0 ), m_iPid( DiscordIpc::GetPid() ) { }
#endif
		void Run();

	private:
		DiscordShared &m_Shared;
		DiscordConnection m_Conn;
		enum { CLOSED, WAIT_READY, READY } m_State;
		CString m_sEndpoint;
		CString m_sAppId;		// what the connection was made with
		CString m_sBuf;			// read and not yet decoded
		int64_t m_iNextTry, m_iLastSend, m_iReadyDeadline;
		CString m_sSent;		// the activity Discord has
		bool m_bSentAny;
		CString m_sLastProblem;	// what Discord last complained about, logged once
		unsigned m_iNonce;
		unsigned m_iPid;
#if !defined(WITHOUT_EXTRAS)
		/* Our own, so the game's never waits for ours (see ChartMSD). */
		minacalc_handle *m_pCalc;

		void DoJob();
#endif
		void Connect( const CString &sAppId, int64_t iNow );
		void Disconnect( const CString &sWhy, bool bLog );
		bool SendActivity( const CString &sActivity );
		void ReadFrames( int iTimeoutMs, int64_t iNow );
		void Problem( const CString &s );
	};
}

void Worker::Problem( const CString &s )
{
	if( s == m_sLastProblem )
		return;
	m_sLastProblem = s;
	LOG->Info( "Discord: %s", s.c_str() );
}

#if !defined(WITHOUT_EXTRAS)
void Worker::DoJob()
{
	int iJob;
	vector<minacalc_row> rows;
	unsigned iKeyCount;
	float fRate;
	{
		LockMut( m_Shared.mutex );
		if( m_Shared.iJob == m_Shared.iJobDone )
			return;
		iJob = m_Shared.iJob;
		rows = m_Shared.rows;
		iKeyCount = m_Shared.iKeyCount;
		fRate = m_Shared.fRate;
	}

	if( m_pCalc == NULL )
		m_pCalc = minacalc_create();
	ChartMSDTable table;
	float fAtRate[MINACALC_NUM_SKILLSETS];
	const minacalc_row *pRows = rows.empty()? NULL: &rows[0];
	bool bOK = m_pCalc != NULL;
	if( bOK && fRate == 0 )
		bOK = minacalc_msd( m_pCalc, pRows, rows.size(), iKeyCount, &table.fMSD[0][0] ) == MINACALC_OK;
	else if( bOK )
		bOK = minacalc_msd_at_rate( m_pCalc, pRows, rows.size(), iKeyCount, fRate, fAtRate ) == MINACALC_OK;

	LockMut( m_Shared.mutex );
	if( m_Shared.iJob != iJob )
		return;	// something else is wanted now
	m_Shared.iJobDone = iJob;
	m_Shared.bJobOK = bOK;
	if( bOK && fRate == 0 )
		m_Shared.table = table;
	else if( bOK )
		memcpy( m_Shared.fAtRate, fAtRate, sizeof(fAtRate) );
}
#endif

void Worker::Connect( const CString &sAppId, int64_t iNow )
{
	m_iNextTry = iNow + RETRY_MS;
	vector<CString> vEndpoints;
	DiscordIpc::GetEndpoints( vEndpoints );
	for( unsigned i = 0; i < vEndpoints.size(); ++i )
	{
		if( !m_Conn.Open(vEndpoints[i]) )
			continue;
		m_sEndpoint = vEndpoints[i];
		m_sAppId = sAppId;
		m_sBuf = "";
		if( !m_Conn.Write(DiscordIpc::EncodeFrame(DiscordIpc::OP_HANDSHAKE, DiscordIpc::Handshake(sAppId))) )
		{
			m_Conn.Close();
			continue;
		}
		m_State = WAIT_READY;
		m_iReadyDeadline = iNow + READY_TIMEOUT_MS;
		return;
	}
}

void Worker::Disconnect( const CString &sWhy, bool bLog )
{
	if( m_State == READY && bLog )
		LOG->Info( "Discord: disconnected (%s)", sWhy.c_str() );
	if( m_State == READY )
		m_iNextTry = DiscordIpc::NowMs() + RECONNECT_MS;
	m_Conn.Close();
	m_State = CLOSED;
	m_sBuf = "";
	m_bSentAny = false;
}

bool Worker::SendActivity( const CString &sActivity )
{
	const CString sNonce = ssprintf( "%u-%u", m_iPid, ++m_iNonce );
	const CString sCmd = DiscordIpc::SetActivity( m_iPid, sActivity, sNonce );
	if( !m_Conn.Write(DiscordIpc::EncodeFrame(DiscordIpc::OP_FRAME, sCmd)) )
		return false;
	m_sSent = sActivity;
	m_bSentAny = true;
	m_iLastSend = DiscordIpc::NowMs();
	return true;
}

void Worker::ReadFrames( int iTimeoutMs, int64_t iNow )
{
	/* What came before the connection closed still counts: Discord says why
	 * it closes a connection, then closes it. */
	const bool bOpen = m_Conn.Read( m_sBuf, iTimeoutMs );

	for(;;)
	{
		int iOpcode;
		CString sJson;
		const DiscordIpc::DecodeResult r = DiscordIpc::DecodeFrame( m_sBuf, iOpcode, sJson );
		if( r == DiscordIpc::DECODE_NEED_MORE )
			break;
		if( r == DiscordIpc::DECODE_BAD )
		{
			Disconnect( "bad data", true );
			return;
		}

		JsonValue v;
		CString sError;
		JsonValue::Parse( sJson, v, sError );

		switch( iOpcode )
		{
		case DiscordIpc::OP_PING:
			if( !m_Conn.Write(DiscordIpc::EncodeFrame(DiscordIpc::OP_PONG, sJson)) )
			{
				Disconnect( "connection closed", true );
				return;
			}
			break;
		case DiscordIpc::OP_CLOSE:
		{
			/* Discord says why: a bad application ID, say. */
			const CString sMessage = v["message"].GetString();
			const int iCode = v["code"].GetInt( 0 );
			if( m_State != READY )
				Problem( ssprintf("refused the connection: %s (%i)", sMessage.c_str(), iCode) );
			Disconnect( ssprintf("%s (%i)", sMessage.c_str(), iCode), true );
			return;
		}
		case DiscordIpc::OP_FRAME:
		{
			const CString sEvent = v["evt"].GetString();
			if( sEvent == "READY" && m_State == WAIT_READY )
			{
				m_State = READY;
				m_sLastProblem = "";
				LOG->Info( "Discord: connected (%s)", EndpointName(m_sEndpoint).c_str() );
			}
			else if( sEvent == "ERROR" )
			{
				Problem( ssprintf("%s: %s", v["cmd"].GetString().c_str(), v["data"]["message"].GetString().c_str()) );
			}
			break;
		}
		default:
			break;	// a PONG or a HANDSHAKE we don't expect: ignore it
		}
	}

	if( !bOpen )
	{
		Disconnect( "connection closed", true );
		return;
	}

	if( m_State == WAIT_READY && iNow > m_iReadyDeadline )
	{
		Problem( "no answer to the handshake" );
		Disconnect( "no answer", false );
	}
}

void Worker::Run()
{
	for(;;)
	{
		bool bEnabled;
		CString sAppId, sActivity;
		{
			LockMut( m_Shared.mutex );
			if( m_Shared.bQuit )
				break;
			bEnabled = m_Shared.bEnabled;
			sAppId = m_Shared.sAppId;
			sActivity = m_Shared.sActivity;
		}

#if !defined(WITHOUT_EXTRAS)
		DoJob();
#endif

		const int64_t iNow = DiscordIpc::NowMs();

		/* Turned off, or another application: let go of this connection. */
		if( m_State != CLOSED && (!bEnabled || sAppId != m_sAppId) )
		{
			if( m_State == READY )
				SendActivity( "" );
			Disconnect( bEnabled? "application ID changed": "turned off", true );
			m_iNextTry = 0;
		}
		if( !bEnabled )
		{
			m_Shared.wake.TimedWait( 100 );
			continue;
		}

		if( m_State == CLOSED )
		{
			if( iNow < m_iNextTry )
			{
				m_Shared.wake.TimedWait( 100 );
				continue;
			}
			Connect( sAppId, iNow );
			if( m_State == CLOSED )
				continue;
		}

		ReadFrames( 0, iNow );

		if( m_State == READY && (!m_bSentAny || sActivity != m_sSent) && iNow - m_iLastSend >= MIN_UPDATE_MS )
			if( !SendActivity(sActivity) )
				Disconnect( "connection closed", true );

		/* Nap here rather than in the socket, so quitting can wake us. */
		m_Shared.wake.TimedWait( 100 );
	}

	/* Quitting: clear the activity and give Discord a moment to take it. */
	if( m_State == READY && SendActivity("") )
	{
		const int64_t iUntil = DiscordIpc::NowMs() + 500;
		CString sBuf;
		while( m_Conn.IsOpen() && DiscordIpc::NowMs() < iUntil )
		{
			if( !m_Conn.Read(sBuf, 50) )
				break;
			int iOpcode;
			CString sJson;
			if( DiscordIpc::DecodeFrame(sBuf, iOpcode, sJson) != DiscordIpc::DECODE_NEED_MORE )
				break;
		}
	}
	m_Conn.Close();
}

static int WorkerMain( void *p )
{
	DiscordShared *pShared = (DiscordShared *) p;
	{
		Worker w( *pShared );
		w.Run();
	}
	pShared->exited.Post();
	return 0;
}

/*
 * The game's side.
 */

DiscordPresence::DiscordPresence()
{
	m_pShared = NULL;
	m_pThread = NULL;
	m_fSinceLook = LOOK_SECONDS;
	m_bHold = false;
	m_iSessionStartMs = DiscordIpc::NowMs();
	m_pPlaySong = NULL;
	m_pPlaySteps = NULL;
	m_fPlayRate = 1;
	m_iPlayStage = -1;
	m_fPlayTime = 0;
	m_iEndMs = 0;
#if !defined(WITHOUT_EXTRAS)
	m_bHaveMSD = false;
	ZERO( m_fMSD );
	m_iMSDJob = 0;
	m_uMSDHash = 0;
	m_bStoreMSD = false;
#endif
}

DiscordPresence::~DiscordPresence()
{
	if( m_pThread == NULL )
		return;

	{
		LockMut( m_pShared->mutex );
		m_pShared->bQuit = true;
	}
	m_pShared->wake.Post();

	/* The thread never waits long on anything, but don't hang the exit on
	 * Discord: if it doesn't finish in time, leave it be. */
	if( !m_pShared->exited.TimedWait(1500) )
		return;	// m_pShared and m_pThread are left to the thread
	m_pThread->Wait();
	delete m_pThread;
	delete m_pShared;
}

void DiscordPresence::StartThread()
{
	m_pShared = new DiscordShared;
	m_pThread = new RageThread;
	m_pThread->SetName( "Discord" );
	m_pThread->Create( WorkerMain, m_pShared );
}

void DiscordPresence::Update( float fDeltaTime )
{
	m_fSinceLook += fDeltaTime;
	if( m_fSinceLook < LOOK_SECONDS )
		return;
	const float fElapsed = m_fSinceLook;
	m_fSinceLook = 0;

	CString sAppId = PREFSMAN->m_sDiscordAppId;
	TrimLeft( sAppId );
	TrimRight( sAppId );
	const bool bEnabled = PREFSMAN->m_bDiscordRichPresence && !sAppId.empty();
	if( !bEnabled )
	{
		if( m_pThread != NULL )
		{
			LockMut( m_pShared->mutex );
			m_pShared->bEnabled = false;
		}
		return;
	}

	if( m_pThread == NULL )
		StartThread();

	m_fPlayTime += fElapsed;
#if !defined(WITHOUT_EXTRAS)
	CollectMSD();
#endif
	m_bHold = false;
	const CString sActivity = BuildActivity();

	LockMut( m_pShared->mutex );
	m_pShared->bEnabled = true;
	m_pShared->sAppId = sAppId;
	if( !m_bHold )
		m_pShared->sActivity = sActivity;
}

/* The chart that's starting, and its MSD, if it has one: from what's known
 * already, or else worked out on the thread. */
void DiscordPresence::StartSong( const Song *pSong, const Steps *pSteps, float fRate )
{
	m_pPlaySong = pSong;
	m_pPlaySteps = pSteps;
	m_fPlayRate = fRate;
	m_fPlayTime = 0;
	m_iEndMs = 0;
#if !defined(WITHOUT_EXTRAS)
	m_bHaveMSD = false;
	m_iMSDJob = 0;
	if( m_bStoreMSD )
		ChartMSD::StoreMSD( m_sMSDKey, m_uMSDHash, m_MSDTable, true );
	m_bStoreMSD = false;

	vector<minacalc_row> rows;
	if( !ChartMSD::GetRows(pSong, pSteps, rows) )
		return;
	m_sMSDKey = WifeManager::GetChartKey( pSong, pSteps );
	if( m_sMSDKey.empty() )
		return;
	m_uMSDHash = ChartMSD::HashRows( rows );

	ChartMSDTable table;
	if( fRate >= 0.7f && ChartMSD::LookupMSD(m_sMSDKey, m_uMSDHash, table) )
	{
		for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
			m_fMSD[s] = table.Get( fRate, s );
		m_bHaveMSD = true;
		return;
	}

	LockMut( m_pShared->mutex );
	m_pShared->rows = rows;
	m_pShared->iKeyCount = ChartMSD::GetKeyCount( pSteps );
	m_pShared->fRate = fRate >= 0.7f? 0: fRate;
	m_iMSDJob = ++m_pShared->iJob;
#endif
}

#if !defined(WITHOUT_EXTRAS)
void DiscordPresence::CollectMSD()
{
	if( m_iMSDJob == 0 )
		return;

	ChartMSDTable table;
	{
		LockMut( m_pShared->mutex );
		if( m_pShared->iJobDone != m_iMSDJob )
			return;
		m_iMSDJob = 0;
		if( !m_pShared->bJobOK )
			return;
		if( m_pShared->fRate != 0 )
		{
			memcpy( m_fMSD, m_pShared->fAtRate, sizeof(m_fMSD) );
			m_bHaveMSD = true;
			return;
		}
		table = m_pShared->table;
	}

	/* Keep it, so the F5 panel doesn't work it out again.  The song cache is
	 * written once the song is over. */
	ChartMSD::StoreMSD( m_sMSDKey, m_uMSDHash, table, false );
	m_MSDTable = table;
	m_bStoreMSD = true;
	for( int s = 0; s < MINACALC_NUM_SKILLSETS; ++s )
		m_fMSD[s] = table.Get( m_fPlayRate, s );
	m_bHaveMSD = true;
}
#endif

/* The player whose chart is shown: the first human one, if any. */
static PlayerNumber MainPlayer()
{
	FOREACH_HumanPlayer( p )
		return p;
	return PLAYER_INVALID;
}

static CString SongLine( const Song *pSong )
{
	CString s = pSong->GetFullDisplayTitle();
	const CString sArtist = pSong->GetDisplayArtist();
	if( !sArtist.empty() )
		s += " - " + sArtist;
	return s;
}

static CString Dot()
{
	return " \xC2\xB7 ";
}

#if !defined(WITHOUT_EXTRAS)
/* "https://aurasm.online/path" -> "aurasm.online" */
static CString UrlHost( const CString &sURL )
{
	CString s = sURL.substr( 8 );
	const size_t i = s.find_first_of( "/?#" );
	if( i != CString::npos )
		s.erase( i );
	return s;
}
#endif

CString DiscordPresence::BuildActivity()
{
	enum { MENUS, IDLE, EDITING, SELECT, PLAYING, RESULTS } state = MENUS;
	Screen *pScreen = SCREENMAN->GetTopScreen();
	ScreenEvaluation *pEval = dynamic_cast<ScreenEvaluation *>( pScreen );

	if( GAMESTATE->m_bDemonstrationOrJukebox || dynamic_cast<ScreenJukebox *>(pScreen) || dynamic_cast<ScreenAttract *>(pScreen) )
		state = IDLE;
	else if( GAMESTATE->m_bEditing || dynamic_cast<ScreenEdit *>(pScreen) || dynamic_cast<ScreenEditMenu *>(pScreen) )
		state = EDITING;
	else if( dynamic_cast<ScreenGameplay *>(pScreen) && GAMESTATE->m_pCurSong != NULL )
		state = PLAYING;
	else if( pEval != NULL )
		state = RESULTS;
	else if( dynamic_cast<ScreenSelectMusic *>(pScreen) || dynamic_cast<ScreenEz2SelectMusic *>(pScreen) ||
		dynamic_cast<ScreenNetSelectMusic *>(pScreen) )
		state = SELECT;
	/* The options and stage screens between choosing a song and playing it. */
	else if( (dynamic_cast<ScreenPlayerOptions *>(pScreen) || dynamic_cast<ScreenSongOptions *>(pScreen) ||
		dynamic_cast<ScreenStage *>(pScreen)) && (GAMESTATE->m_pCurSong != NULL || GAMESTATE->m_pCurCourse != NULL) )
		state = SELECT;

	if( state != PLAYING )
	{
		m_pPlaySong = NULL;
#if !defined(WITHOUT_EXTRAS)
		if( m_bStoreMSD )
			ChartMSD::StoreMSD( m_sMSDKey, m_uMSDHash, m_MSDTable, true );
		m_bStoreMSD = false;
#endif
	}

	DiscordActivity a;
	a.sLargeImage = "logo";
	a.sLargeText = "Stepmania 3.9";
	a.iStartMs = m_iSessionStartMs;

	const PlayerNumber pn = MainPlayer();
	const int iPlayers = GAMESTATE->GetNumHumanPlayers();
	const float fRate = GAMESTATE->m_SongOptions.m_fMusicRate;

	switch( state )
	{
	case MENUS:
		a.sDetails = "In the menus";
		break;
	case IDLE:
		a.sDetails = "Idle";
		break;
	case EDITING:
		a.sDetails = "Editing a chart";
		break;
	case SELECT:
		if( GAMESTATE->IsCourseMode() )
		{
			a.sDetails = "Choosing a course";
			if( GAMESTATE->m_pCurCourse != NULL )
				a.sState = GAMESTATE->m_pCurCourse->GetFullDisplayTitle();
		}
		else
		{
			a.sDetails = "Choosing a song";
			/* The group of the song under the cursor; on a group's own bar,
			 * the last one. */
			if( GAMESTATE->m_pCurSong != NULL )
				m_sLastGroup = GAMESTATE->m_pCurSong->m_sGroupName;
			else if( GAMESTATE->m_sPreferredGroup != GROUP_ALL_MUSIC )
				m_sLastGroup = GAMESTATE->m_sPreferredGroup;
			a.sState = m_sLastGroup;
		}
		break;
	case PLAYING:
	{
		const Song *pSong = GAMESTATE->m_pCurSong;
		const Steps *pSteps = pn == PLAYER_INVALID? NULL: GAMESTATE->m_pCurSteps[pn];
		const bool bCourse = GAMESTATE->IsCourseMode() && GAMESTATE->m_pCurCourse != NULL;
		const int iStage = bCourse? GAMESTATE->GetCourseSongIndex(): 0;
		if( pSong != m_pPlaySong || pSteps != m_pPlaySteps || fRate != m_fPlayRate || iStage != m_iPlayStage )
		{
			m_iPlayStage = iStage;
			StartSong( pSong, bCourse? NULL: pSteps, fRate );	// no MSD in courses
			m_pPlaySteps = pSteps;
		}

		CString sChart;
		if( pSteps != NULL )
		{
#if !defined(WITHOUT_EXTRAS)
			const float fMSD = m_bHaveMSD? m_fMSD[0]: 0;
			int iSkill = m_bHaveMSD? DiscordText::DominantSkillset( m_fMSD ): -1;
			sChart = DiscordText::ChartLine( DifficultyToString(pSteps->GetDifficulty()), pSteps->GetMeter(), fRate,
				fMSD, iSkill == -1? NULL: ChartMSD::SkillsetName(iSkill) );
#else
			sChart = DiscordText::ChartLine( DifficultyToString(pSteps->GetDifficulty()), pSteps->GetMeter(), fRate, 0, NULL );
#endif
		}

		if( bCourse )
		{
			const Course *pCourse = GAMESTATE->m_pCurCourse;
			a.sDetails = pCourse->GetFullDisplayTitle();
			const Trail *pTrail = pn == PLAYER_INVALID? NULL: GAMESTATE->m_pCurTrail[pn];
			const int iStages = pTrail != NULL? (int) pTrail->m_vEntries.size(): pCourse->GetEstimatedNumStages();
			a.sState = pCourse->m_bRepeat || iStages <= 0?
				ssprintf( "Stage %i", iStage + 1 ):
				ssprintf( "Stage %i/%i", iStage + 1, iStages );
			const bool bMystery = pTrail != NULL && iStage < (int) pTrail->m_vEntries.size() && pTrail->m_vEntries[iStage].bMystery;
			if( !bMystery )
				a.sState += Dot() + pSong->GetFullDisplayTitle();
			else if( !sChart.empty() )
				a.sState += Dot() + sChart;
		}
		else
		{
			a.sDetails = SongLine( pSong );
			a.sState = sChart;
		}

		/* The countdown to the end of the song: when the last note has gone by,
		 * as ScreenGameplay sees it.  Worked out again only when it's off by
		 * more than a moment, so it doesn't keep changing. */
		const float fEndSeconds = pSong->GetElapsedTimeFromBeat( pSong->m_fLastBeat );
		const float fLeft = (fEndSeconds - GAMESTATE->m_fMusicSeconds) / max( fRate, 0.01f );
		if( m_fPlayTime >= 0.5f && fLeft > 0 )
		{
			const int64_t iEnd = DiscordIpc::NowMs() + (int64_t) (fLeft * 1000);
			if( m_iEndMs == 0 || iEnd - m_iEndMs > 2000 || m_iEndMs - iEnd > 2000 )
				m_iEndMs = iEnd;
		}
		a.iStartMs = 0;
		a.iEndMs = m_iEndMs;

		/* Wait for the countdown and the MSD (for a moment), so the song goes
		 * out once, complete, rather than in pieces. */
		if( m_iEndMs == 0 && m_fPlayTime < 3 )
			m_bHold = true;
#if !defined(WITHOUT_EXTRAS)
		if( m_iMSDJob != 0 && m_fPlayTime < 3 )
			m_bHold = true;
#endif
		break;
	}
	case RESULTS:
	{
		const bool bCourse = pEval->GetType() == ScreenEvaluation::course;
		if( pEval->GetType() == ScreenEvaluation::summary )
		{
			a.sDetails = "Looking at the results";
			break;
		}
		if( bCourse && GAMESTATE->m_pCurCourse != NULL )
			a.sDetails = GAMESTATE->m_pCurCourse->GetFullDisplayTitle();
		else if( GAMESTATE->m_pCurSong != NULL )
			a.sDetails = GAMESTATE->m_pCurSong->GetFullDisplayTitle();
		else
			a.sDetails = "Looking at the results";
		if( pn == PLAYER_INVALID )
			break;

		const Grade g = g_CurStageStats.GetGrade( pn );
		a.sState = g == GRADE_FAILED? CString("Failed"): GradeToOldString( g );
#if !defined(WITHOUT_EXTRAS)
		if( PREFSMAN->m_bWife && !bCourse && WIFE->HasScore(pn) )
		{
			a.sState += Dot() + ssprintf( "%.2f%% Wife", WIFE->GetFinalPercent(pn) * 100 );
			const WifeStageResult &r = WIFE->GetStageResult( pn );
			if( r.bValid && r.iRankProfile == 1 )
				a.sState += Dot() + "new best";
		}
#endif
		/* Discord wants two characters at least. */
		if( a.sState.size() < 2 )
			a.sState = "Grade " + a.sState;
		break;
	}
	}

	if( iPlayers >= 2 && (state == PLAYING || state == RESULTS || state == SELECT) )
	{
		a.iPartySize = iPlayers;
		a.iPartyMax = iPlayers;
	}

#if !defined(WITHOUT_EXTRAS)
	/* The online scores: a link to the rankings, and the player's profile if
	 * they're logged in. */
	const CString sServer = PREFSMAN->m_sOnlineServer;
	if( sServer.Left(8) == "https://" && sServer.size() > 8 )
	{
		CString sBase = sServer;
		while( sBase.size() > 8 && sBase[sBase.size()-1] == '/' )
			sBase.erase( sBase.size()-1 );
		const CString sHost = UrlHost( sBase );
		a.AddButton( sHost == "aurasm.online"? "AURA rankings": "Online rankings", sBase );

		CString sUser;
		if( ONLINE->IsEnabled() )
		{
			FOREACH_HumanPlayer( p )
			{
				const OnlineAccount &acc = ONLINE->GetAccount( p );
				if( acc.bLoggedIn && !acc.sUsername.empty() )
				{
					sUser = acc.sUsername;
					break;
				}
			}
		}
		if( !sUser.empty() )
		{
			a.sSmallImage = "online";
			a.sSmallText = sUser + " on " + sHost;
			CString sLabel = sUser + "'s profile";
			if( sLabel.size() > 32 )
				sLabel = "Player profile";
			a.AddButton( sLabel, sBase + "/users/" + DiscordText::UrlSegment(sUser) );
		}
	}
#endif

	return a.ToJson();
}
