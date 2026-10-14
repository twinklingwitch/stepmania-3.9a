#include "global.h"
#include "OnlineRoom.h"
#include "OnlineHttp.h"
#include "OnlineRequest.h"
#include "GameState.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "ScreenEvaluation.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "ScreenSelectMusic.h"
#include "SongManager.h"
#include "StageStats.h"
#include "Steps.h"
#include "Style.h"
#include "WifeManager.h"
#include "song.h"

OnlineRoom *ROOM = NULL;	// global and accessable from anywhere in our program

/* Seconds between polls: quicker while everyone is loading, so the song
 * starts soon after the last one is ready. */
static const float POLL_SECONDS = 1.0f;
static const float POLL_LOADING_SECONDS = 0.25f;
/* How long ScreenGameplay waits for the others.  The server gives up on them
 * after 30 seconds and starts without them, so this is only for when it
 * can't be reached. */
static const float WAIT_FOR_START_SECONDS = 40;
static const unsigned KEEP_LINES = 100;
/* Charts by the same title looked at when the room's song isn't where the
 * host has it. */
static const int MAX_TITLE_MATCHES = 30;

static const char *g_szJudgments[NUM_ROOM_JUDGMENTS] =
	{ "marvelous", "perfect", "great", "good", "boo", "miss", "hit_mine", "held", "let_go" };

static float Now()
{
	return RageTimer::GetTimeSinceStart();
}

CString OnlineRoomChart::Describe() const
{
	if( !bSet )
		return "no chart";
	CString s = sTitle;
	if( !sSubtitle.empty() )
		s += " " + sSubtitle;
	return s + ssprintf( " (%s %d) %.2fx", sDifficulty.c_str(), iMeter, fRate );
}

static void ReadChart( const JsonValue &v, OnlineRoomChart &c )
{
	c = OnlineRoomChart();
	if( !v.IsObject() )
		return;
	c.bSet = true;
	c.sChartKey = v["chart_key"].GetString();
	c.sSongDir = v["song_dir"].GetString();
	c.sDifficulty = v["difficulty"].GetString();
	c.iMeter = v["meter"].GetInt();
	c.sTitle = v["title"].GetString();
	c.sSubtitle = v["subtitle"].GetString();
	c.sArtist = v["artist"].GetString();
	c.fRate = (float) v["rate"].GetNumber( 1 );
}

static void ReadScore( const JsonValue &v, OnlineRoomScore &s )
{
	s = OnlineRoomScore();
	if( !v.IsObject() )
		return;
	s.bSet = true;
	s.fWife = (float) v["wife"].GetNumber();
	s.iCombo = v["combo"].GetInt();
	s.iMaxCombo = v["max_combo"].GetInt();
	for( int i = 0; i < NUM_ROOM_JUDGMENTS; ++i )
		s.iJudgments[i] = v["judgments"][g_szJudgments[i]].GetInt();
	s.bFailed = v["failed"].GetBool();
}

/* "Group/Folder", as rooms name a song. */
static CString SongDirName( const Song *pSong )
{
	CStringArray bits;
	split( pSong->GetSongDir(), "/", bits );
	return pSong->m_sGroupName + "/" + (bits.empty()? CString(): bits.back());
}

static Steps *FindStepsByKey( Song *pSong, const CString &sKey )
{
	const vector<Steps*> &v = pSong->GetAllSteps( GAMESTATE->GetCurrentStyle()->m_StepsType );
	for( unsigned i = 0; i < v.size(); ++i )
		if( WifeManager::GetChartKey(pSong, v[i]) == sKey )
			return v[i];
	return NULL;
}

OnlineRoom::OnlineRoom()
{
	m_SessionPlayer = PLAYER_INVALID;
	m_iRoomID = 0;
	m_bJoining = false;
	m_iRound = 0;
	m_bLocked = false;
	m_iVersion = -1;
	m_iLastLine = -1;
	m_iLocalLine = 0;
	m_iLastRound = 0;
	m_iSerial = 0;
	m_iPollRequest = m_iActionRequest = m_iListRequest = 0;
	m_fNextPoll = 0;
	m_pSong = NULL;
	m_pSteps = NULL;
	m_iStartedRound = m_iLoadedRound = m_iGameplayRound = m_iDoneRound = 0;
	m_fWaitingSince = 0;
	m_fLiveWife = m_fFinalWife = 0;
}

bool OnlineRoom::CanUse()
{
	PlayerNumber pn;
	CString sToken, sUser;
	return ONLINE->GetSession( pn, sToken, sUser );
}

/* Out of the room, whatever the reason. */
void OnlineRoom::Reset( const CString &sWhy )
{
	if( m_iPollRequest )
		OnlineHttp::Forget( m_iPollRequest );
	if( m_iActionRequest )
		OnlineHttp::Forget( m_iActionRequest );
	m_iPollRequest = m_iActionRequest = 0;
	m_Actions.clear();

	m_iRoomID = 0;
	m_bJoining = false;
	m_sName = m_sState = "";
	m_iRound = 0;
	m_bLocked = false;
	m_iVersion = -1;
	m_Chart = OnlineRoomChart();
	m_Members.clear();
	m_Lines.clear();
	m_iLastLine = -1;
	m_iLastRound = 0;
	m_LastChart = OnlineRoomChart();
	m_LastResults.clear();
	m_sLocatedKey = m_sSelectedKey = m_sReportedKey = "";
	m_pSong = NULL;
	m_pSteps = NULL;
	m_sChartProblem = "";
	m_iStartedRound = m_iLoadedRound = m_iGameplayRound = m_iDoneRound = 0;
	if( !sWhy.empty() )
		Notice( sWhy );
	++m_iSerial;
}

void OnlineRoom::Notice( const CString &sText )
{
	OnlineChatLine line;
	line.iID = --m_iLocalLine;
	line.kind = OnlineChatLine::NOTICE;
	line.sText = sText;
	line.tAt = time(NULL);
	m_Lines.push_back( line );
	if( m_Lines.size() > KEEP_LINES )
		m_Lines.erase( m_Lines.begin(), m_Lines.end() - KEEP_LINES );
	++m_iSerial;
}

bool OnlineRoom::IsHost() const
{
	const OnlineRoomMember *pMe = GetMe();
	return pMe != NULL && pMe->bHost;
}

const OnlineRoomMember *OnlineRoom::GetMe() const
{
	for( unsigned i = 0; i < m_Members.size(); ++i )
		if( m_Members[i].sUser.CompareNoCase(m_sUser) == 0 )
			return &m_Members[i];
	return NULL;
}

bool OnlineRoom::IsInRound() const
{
	if( !InRoom() || (m_sState != "loading" && m_sState != "playing") )
		return false;
	const OnlineRoomMember *pMe = GetMe();
	return pMe != NULL && pMe->bPlaying;
}

/*
 * Requests
 */

void OnlineRoom::Queue( const CString &sMethod, const CString &sPath, const JsonValue &body )
{
	Action a;
	a.sMethod = sMethod;
	a.sPath = sPath;
	a.body = body;
	m_Actions.push_back( a );
	SendNextAction();
}

void OnlineRoom::SendNextAction()
{
	if( m_iActionRequest || m_Actions.empty() || m_sToken.empty() )
		return;
	Action a = m_Actions.front();
	m_Actions.erase( m_Actions.begin() );
	if( m_iLastLine >= 0 )
		a.body.Set( "after", JsonValue::Int(m_iLastLine) );
	m_iActionRequest = OnlineRequest( m_sServer, a.sMethod, a.sPath, a.body.Write(), m_sToken, 15 );
}

void OnlineRoom::RefreshList()
{
	if( m_iListRequest || m_sToken.empty() )
		return;
	m_iListRequest = OnlineRequest( m_sServer, "GET", "/rooms", "", m_sToken, 15 );
	++m_iSerial;
}

void OnlineRoom::ReadRoom( const OnlineHttpResponse &r, bool bAction )
{
	JsonValue v;
	if( (r.iStatus == 200 || r.iStatus == 201) && OnlineParseBody(r, v) && v["room"].IsObject() )
	{
		m_bJoining = false;
		ParseRoom( v["room"] );
		return;
	}
	if( r.iStatus == 204 )
	{
		Reset( "You left the room." );
		return;
	}
	if( r.iStatus == 401 )
	{
		ONLINE->SessionExpired( m_SessionPlayer );
		Reset( "Your login has expired.  Log in again." );
		return;
	}

	CString sCode;
	const CString sMessage = OnlineErrorMessage( r, &sCode );
	CString sReason;
	if( OnlineParseBody(r, v) )
		sReason = v["error"]["details"]["reason"].GetString();
	if( InRoom() && (sReason == "not_in_room" || (r.iStatus == 404 && !m_bJoining)) )
	{
		Reset( "You aren't in the room any more." );
		return;
	}
	m_bJoining = false;
	/* A poll that didn't get through is tried again; say so only for what we did. */
	if( bAction || !InRoom() )
		Notice( sMessage );
	if( r.iRetryAfter > 0 )
		m_fNextPoll = Now() + min( r.iRetryAfter, 60 );
	++m_iSerial;
}

void OnlineRoom::ParseRoom( const JsonValue &v )
{
	const int iID = v["id"].GetInt();
	if( iID != m_iRoomID )
	{
		/* A new room: what we had was the old one's. */
		const vector<OnlineChatLine> notices = m_Lines;
		Reset( "" );
		m_iRoomID = iID;
		for( unsigned i = 0; i < notices.size(); ++i )
			if( notices[i].kind == OnlineChatLine::NOTICE )
				m_Lines.push_back( notices[i] );
	}

	/* Lines come in any answer; take the ones we haven't seen. */
	const JsonValue &lines = v["lines"];
	for( unsigned i = 0; i < lines.size(); ++i )
	{
		const JsonValue &l = lines[i];
		OnlineChatLine line;
		line.iID = l["id"].GetInt();
		if( line.iID <= m_iLastLine )
			continue;
		const CString sKind = l["kind"].GetString();
		line.kind = sKind == "action"? OnlineChatLine::ACTION: sKind == "system"? OnlineChatLine::SYSTEM: OnlineChatLine::MESSAGE;
		line.sUser = l["user"]["username"].GetString();
		line.sText = l["text"].GetString();
		line.tAt = OnlineParseTime( l["at"].GetString() );
		m_Lines.push_back( line );
		m_iLastLine = line.iID;
	}
	if( m_Lines.size() > KEEP_LINES )
		m_Lines.erase( m_Lines.begin(), m_Lines.end() - KEEP_LINES );
	m_iLastLine = max( m_iLastLine, v["last_line"].GetInt(m_iLastLine) );
	++m_iSerial;

	/* An answer older than one we already have says nothing new. */
	const int iVersion = v["version"].GetInt();
	if( iVersion < m_iVersion )
		return;
	m_iVersion = iVersion;

	m_sName = v["name"].GetString();
	m_sState = v["state"].GetString();
	m_iRound = v["round"].GetInt();
	m_bLocked = v["locked"].GetBool();
	ReadChart( v["chart"], m_Chart );

	m_Members.clear();
	const JsonValue &members = v["members"];
	for( unsigned i = 0; i < members.size(); ++i )
	{
		const JsonValue &m = members[i];
		OnlineRoomMember member;
		member.sUser = m["user"]["username"].GetString();
		member.bHost = m["host"].GetBool();
		member.bReady = m["ready"].GetBool();
		member.bHasChart = m["has_chart"].GetBool();
		member.bPlaying = m["playing"].GetBool();
		member.bLoaded = m["loaded"].GetBool();
		member.bDone = m["done"].GetBool();
		ReadScore( m["progress"], member.score );
		m_Members.push_back( member );
	}

	const JsonValue &last = v["last_round"];
	if( last.IsObject() )
	{
		m_iLastRound = last["round"].GetInt();
		ReadChart( last["chart"], m_LastChart );
		m_LastResults.clear();
		const JsonValue &results = last["results"];
		for( unsigned i = 0; i < results.size(); ++i )
		{
			OnlineRoomResult res;
			res.iRank = results[i]["rank"].GetInt();
			res.sUser = results[i]["user"]["username"].GetString();
			res.bFinished = results[i]["finished"].GetBool();
			ReadScore( results[i]["progress"], res.score );
			m_LastResults.push_back( res );
		}
	}
}

/*
 * Things to do
 */

void OnlineRoom::Create( const CString &sName, const CString &sPassword )
{
	JsonValue v = JsonValue::Object();
	v.Set( "name", JsonValue::String(sName) );
	v.Set( "password", JsonValue::String(sPassword) );
	m_bJoining = true;
	Queue( "POST", "/rooms", v );
}

void OnlineRoom::Join( int iID, const CString &sPassword )
{
	JsonValue v = JsonValue::Object();
	v.Set( "password", JsonValue::String(sPassword) );
	m_bJoining = true;
	Queue( "POST", ssprintf("/rooms/%d/join", iID), v );
}

void OnlineRoom::Leave()
{
	if( !InRoom() )
		return;
	Queue( "POST", ssprintf("/rooms/%d/leave", m_iRoomID), JsonValue::Object() );
}

void OnlineRoom::Say( const CString &sText )
{
	JsonValue v = JsonValue::Object();
	v.Set( "text", JsonValue::String(sText) );
	Queue( "POST", ssprintf("/rooms/%d/chat", m_iRoomID), v );
}

void OnlineRoom::SetReady( bool bReady )
{
	if( bReady && !HaveChart() )
	{
		Notice( m_Chart.bSet? "You can't be ready without the chart: " + m_sChartProblem: CString("There's no chart yet.") );
		return;
	}
	JsonValue v = JsonValue::Object();
	v.Set( "ready", JsonValue::Bool(bReady) );
	if( bReady )
		v.Set( "has_chart", JsonValue::Bool(true) );
	Queue( "POST", ssprintf("/rooms/%d/me", m_iRoomID), v );
}

void OnlineRoom::PickCurrent()
{
	if( !IsHost() )
	{
		Notice( "Only the host picks the chart." );
		return;
	}
	const PlayerNumber pn = GAMESTATE->GetFirstHumanPlayer();
	Song *pSong = GAMESTATE->m_pCurSong;
	Steps *pSteps = pn == PLAYER_INVALID? NULL: GAMESTATE->m_pCurSteps[pn];
	if( pSong == NULL || pSteps == NULL || GAMESTATE->IsCourseMode() )
	{
		Notice( "Choose a song and a difficulty on the music select screen first." );
		return;
	}
	JsonValue c = JsonValue::Object();
	c.Set( "chart_key", JsonValue::String(WifeManager::GetChartKey(pSong, pSteps)) );
	c.Set( "song_dir", JsonValue::String(SongDirName(pSong)) );
	c.Set( "difficulty", JsonValue::String(DifficultyToString(pSteps->GetDifficulty())) );
	c.Set( "meter", JsonValue::Int(pSteps->GetMeter()) );
	c.Set( "title", JsonValue::String(pSong->GetTranslitMainTitle()) );
	c.Set( "subtitle", JsonValue::String(pSong->GetTranslitSubTitle()) );
	c.Set( "artist", JsonValue::String(pSong->GetTranslitArtist()) );
	c.Set( "rate", JsonValue::Float(GAMESTATE->m_SongOptions.m_fMusicRate) );
	JsonValue v = JsonValue::Object();
	v.Set( "chart", c );
	Queue( "POST", ssprintf("/rooms/%d/chart", m_iRoomID), v );
}

void OnlineRoom::Start( bool bForce )
{
	JsonValue v = JsonValue::Object();
	v.Set( "force", JsonValue::Bool(bForce) );
	Queue( "POST", ssprintf("/rooms/%d/start", m_iRoomID), v );
}

void OnlineRoom::Kick( const CString &sUser )
{
	JsonValue v = JsonValue::Object();
	v.Set( "username", JsonValue::String(sUser) );
	Queue( "POST", ssprintf("/rooms/%d/kick", m_iRoomID), v );
}

void OnlineRoom::GiveHost( const CString &sUser )
{
	JsonValue v = JsonValue::Object();
	v.Set( "username", JsonValue::String(sUser) );
	Queue( "POST", ssprintf("/rooms/%d/host", m_iRoomID), v );
}

void OnlineRoom::Rename( const CString &sName )
{
	JsonValue v = JsonValue::Object();
	v.Set( "text", JsonValue::String(sName) );
	Queue( "POST", ssprintf("/rooms/%d/name", m_iRoomID), v );
}

void OnlineRoom::SetPassword( const CString &sPassword )
{
	JsonValue v = JsonValue::Object();
	v.Set( "password", JsonValue::String(sPassword) );
	Queue( "POST", ssprintf("/rooms/%d/password", m_iRoomID), v );
}

/*
 * The chart
 */

void OnlineRoom::LocateChart()
{
	if( !m_Chart.bSet )
	{
		m_sLocatedKey = "";
		m_pSong = NULL;
		m_pSteps = NULL;
		m_sChartProblem = "";
		return;
	}
	/* Until a style is chosen there's nothing to look in. */
	if( GAMESTATE->GetCurrentStyle() == NULL )
	{
		m_sChartProblem = "Choose a style first.";
		return;
	}
	if( m_sLocatedKey != m_Chart.sChartKey )
	{
		m_sLocatedKey = m_Chart.sChartKey;
		m_pSong = NULL;
		m_pSteps = NULL;

		/* Where the host has it, then anything by the same title. */
		Song *pSong = SONGMAN->FindSong( m_Chart.sSongDir );
		Steps *pSteps = pSong? FindStepsByKey( pSong, m_Chart.sChartKey ): NULL;
		if( pSteps == NULL )
		{
			const vector<Song*> &all = SONGMAN->GetAllSongs();
			int iTried = 0;
			for( unsigned i = 0; i < all.size() && pSteps == NULL && iTried < MAX_TITLE_MATCHES; ++i )
			{
				if( all[i] == pSong || all[i]->GetTranslitMainTitle().CompareNoCase(m_Chart.sTitle) != 0 )
					continue;
				++iTried;
				pSteps = FindStepsByKey( all[i], m_Chart.sChartKey );
				if( pSteps )
					pSong = all[i];
			}
		}
		if( pSteps )
		{
			m_pSong = pSong;
			m_pSteps = pSteps;
			m_sChartProblem = "";
		}
		else if( pSong )
			m_sChartProblem = "You have the song (" + m_Chart.sSongDir + ") but not this chart: your copy is different.";
		else
			m_sChartProblem = "You don't have " + m_Chart.sSongDir + ".";
		++m_iSerial;
	}

	/* Tell the room whether we have it, once for each chart. */
	const OnlineRoomMember *pMe = GetMe();
	if( pMe != NULL && pMe->bHasChart != HaveChart() && m_sReportedKey != m_sLocatedKey && m_sState == "lobby" )
	{
		m_sReportedKey = m_sLocatedKey;
		JsonValue v = JsonValue::Object();
		v.Set( "has_chart", JsonValue::Bool(HaveChart()) );
		Queue( "POST", ssprintf("/rooms/%d/me", m_iRoomID), v );
	}
}

/*
 * The round
 */

JsonValue OnlineRoom::ProgressBody( bool bDone, bool bQuit ) const
{
	const PlayerNumber pn = GAMESTATE->GetFirstHumanPlayer();
	JsonValue p = JsonValue::Object();
	JsonValue j = JsonValue::Object();
	if( pn != PLAYER_INVALID )
	{
		/* Wife isn't active any more once the results are up: take what it
		 * said on gameplay's last frame. */
		p.Set( "wife", JsonValue::Float(bDone? m_fFinalWife: m_fLiveWife) );
		p.Set( "combo", JsonValue::Int(g_CurStageStats.iCurCombo[pn]) );
		p.Set( "max_combo", JsonValue::Int(g_CurStageStats.iMaxCombo[pn]) );
		const int *tns = g_CurStageStats.iTapNoteScores[pn];
		const int *hns = g_CurStageStats.iHoldNoteScores[pn];
		const int counts[NUM_ROOM_JUDGMENTS] = { tns[TNS_MARVELOUS], tns[TNS_PERFECT], tns[TNS_GREAT], tns[TNS_GOOD],
			tns[TNS_BOO], tns[TNS_MISS], tns[TNS_HIT_MINE], hns[HNS_OK], hns[HNS_NG] };
		for( int i = 0; i < NUM_ROOM_JUDGMENTS; ++i )
			j.Set( g_szJudgments[i], JsonValue::Int(counts[i]) );
		p.Set( "failed", JsonValue::Bool(g_CurStageStats.bFailed[pn]) );
	}
	p.Set( "judgments", j );
	JsonValue v = JsonValue::Object();
	v.Set( "progress", p );
	v.Set( "done", JsonValue::Bool(bDone) );
	v.Set( "quit", JsonValue::Bool(bQuit) );
	return v;
}

void OnlineRoom::Loaded()
{
	if( !IsInRound() || m_iLoadedRound == m_iRound )
		return;
	m_iLoadedRound = m_iRound;
	m_fWaitingSince = Now();
	Queue( "POST", ssprintf("/rooms/%d/loaded", m_iRoomID), JsonValue::Object() );
	m_fNextPoll = 0;
}

bool OnlineRoom::MayStart() const
{
	if( !IsInRound() || m_sState == "playing" )
		return true;
	return Now() - m_fWaitingSince > WAIT_FOR_START_SECONDS;
}

void OnlineRoom::UpdateRound()
{
	Screen *pTop = SCREENMAN->GetTopScreen();
	ScreenSelectMusic *pSelect = dynamic_cast<ScreenSelectMusic *>( pTop );
	const bool bGameplay = dynamic_cast<ScreenGameplay *>( pTop ) != NULL;

	/* Put the wheel on a new chart once, and leave the player to look around. */
	const CString sSelect = m_sLocatedKey + ssprintf( "@%.2f", m_Chart.fRate );
	if( pSelect != NULL && HaveChart() && m_sState == "lobby" && m_sSelectedKey != sSelect )
		if( pSelect->SelectRoomChart(m_pSong, m_pSteps, m_Chart.fRate) )
			m_sSelectedKey = sSelect;

	if( !IsInRound() )
		return;
	if( bGameplay )
	{
		if( m_iGameplayRound != m_iRound )
			m_fLiveWife = m_fFinalWife = 0;
		m_iGameplayRound = m_iRound;
		const PlayerNumber pn = GAMESTATE->GetFirstHumanPlayer();
		if( pn != PLAYER_INVALID && WIFE->IsActive(pn) )
		{
			m_fLiveWife = WIFE->GetLivePercent( pn );
			m_fFinalWife = WIFE->GetFinalPercent( pn );
		}
	}

	/* The round is starting: start the song as if Start was pressed. */
	if( m_sState == "loading" && m_iStartedRound != m_iRound && pSelect != NULL && HaveChart() )
	{
		if( pSelect->SelectRoomChart(m_pSong, m_pSteps, m_Chart.fRate) )
		{
			pSelect->StartRoomRound();
			m_iStartedRound = m_iRound;
		}
	}

	/* Gameplay is over for us, one way or another: send how it went.  A song
	 * played to the end (or failed) goes on to the results; one given up on
	 * goes back to the music select. */
	if( m_iGameplayRound == m_iRound && !bGameplay && m_iDoneRound != m_iRound )
	{
		m_iDoneRound = m_iRound;
		const bool bQuit = dynamic_cast<ScreenEvaluation *>( pTop ) == NULL;
		Queue( "POST", ssprintf("/rooms/%d/progress", m_iRoomID), ProgressBody(true, bQuit) );
	}
}

void OnlineRoom::Update( float fDeltaTime )
{
	/* The session: if the account changes, we're out of the room. */
	PlayerNumber pn = PLAYER_INVALID;
	CString sToken, sUser;
	if( !ONLINE->GetSession(pn, sToken, sUser) )
	{
		pn = PLAYER_INVALID;
		sToken = sUser = "";
	}
	if( sToken != m_sToken )
	{
		if( InRoom() || m_bJoining )
			Reset( sToken.empty()? "You logged out, so you left the room.": "You logged in again, so you left the room." );
		if( m_iListRequest )
			OnlineHttp::Forget( m_iListRequest );
		m_iListRequest = 0;
		m_List.clear();
		m_sListError = "";
		m_sToken = sToken;
		m_sUser = sUser;
		m_SessionPlayer = pn;
		m_sServer = ONLINE->GetServerURL();
		++m_iSerial;
	}
	if( m_sToken.empty() )
		return;

	OnlineHttpResponse r;
	if( m_iListRequest && OnlineHttp::Poll(m_iListRequest, r) )
	{
		m_iListRequest = 0;
		JsonValue v;
		m_List.clear();
		if( r.iStatus == 200 && OnlineParseBody(r, v) )
		{
			m_sListError = "";
			const JsonValue &rooms = v["rooms"];
			for( unsigned i = 0; i < rooms.size(); ++i )
			{
				const JsonValue &e = rooms[i];
				OnlineRoomListing l;
				l.iID = e["id"].GetInt();
				l.sName = e["name"].GetString();
				l.sHost = e["host"]["username"].GetString();
				l.sState = e["state"].GetString();
				l.bLocked = e["locked"].GetBool();
				l.iPlayers = e["players"].GetInt();
				l.iMaxPlayers = e["max_players"].GetInt();
				ReadChart( e["chart"], l.chart );
				m_List.push_back( l );
			}
		}
		else
		{
			if( r.iStatus == 401 )
				ONLINE->SessionExpired( m_SessionPlayer );
			m_sListError = OnlineErrorMessage( r );
		}
		++m_iSerial;
	}
	if( m_iActionRequest && OnlineHttp::Poll(m_iActionRequest, r) )
	{
		m_iActionRequest = 0;
		ReadRoom( r, true );
	}
	if( m_iPollRequest && OnlineHttp::Poll(m_iPollRequest, r) )
	{
		m_iPollRequest = 0;
		ReadRoom( r, false );
	}
	SendNextAction();

	if( !InRoom() )
		return;
	LocateChart();
	UpdateRound();

	if( m_iPollRequest == 0 && Now() >= m_fNextPoll )
	{
		/* While we play, our progress is the poll. */
		const bool bGameplay = dynamic_cast<ScreenGameplay *>( SCREENMAN->GetTopScreen() ) != NULL;
		if( IsInRound() && m_sState == "playing" && bGameplay && m_iDoneRound != m_iRound )
		{
			JsonValue v = ProgressBody( false, false );
			if( m_iLastLine >= 0 )
				v.Set( "after", JsonValue::Int(m_iLastLine) );
			m_iPollRequest = OnlineRequest( m_sServer, "POST", ssprintf("/rooms/%d/progress", m_iRoomID), v.Write(), m_sToken, 10 );
		}
		else
		{
			CString sPath = ssprintf( "/rooms/%d", m_iRoomID );
			if( m_iLastLine >= 0 )
				sPath += ssprintf( "?after=%d", m_iLastLine );
			m_iPollRequest = OnlineRequest( m_sServer, "GET", sPath, "", m_sToken, 10 );
		}
		m_fNextPoll = Now() + (m_sState == "loading"? POLL_LOADING_SECONDS: POLL_SECONDS);
	}
}
