#include "global.h"
#include "RoomOverlay.h"
#include "OnlineRoom.h"
#include "ChartInfo.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "ScreenEvaluation.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "ThemeManager.h"

#include <algorithm>

/* The room list is asked for again this often while it's shown. */
static const float LIST_REFRESH_SECONDS = 5;

static const float BAR_RIGHT = SCREEN_WIDTH - 8, BAR_TOP = 8;
static const float BAR_ZOOM = 0.5f;

using ChartInfo::PercentString;

RoomOverlay::RoomOverlay(): ChatPanel( KEY_F10 )
{
	m_iSerial = -1;
	m_fListAge = 0;
	m_bBarInit = false;
	m_bBarVisible = false;
}

void RoomOverlay::SetOpen( bool bOpen )
{
	ChatPanel::SetOpen( bOpen );
	m_iSerial = -1;
	if( bOpen && !ROOM->InRoom() )
	{
		ROOM->RefreshList();
		m_fListAge = 0;
	}
}

static CString StateText()
{
	const CString sState = ROOM->GetState();
	if( sState == "loading" )
		return ssprintf( "round %d: loading", ROOM->GetRound() );
	if( sState == "playing" )
		return ssprintf( "round %d: playing", ROOM->GetRound() );
	return sState;
}

static CString ScoreText( const OnlineRoomScore &s )
{
	CString sText = PercentString( s.fWife );
	if( s.bFailed )
		sText += " failed";
	return sText;
}

/* The members playing the round, best first. */
static bool BetterScore( const OnlineRoomMember *a, const OnlineRoomMember *b )
{
	return a->score.fWife > b->score.fWife;
}

static void Playing( const vector<OnlineRoomMember> &members, vector<const OnlineRoomMember *> &out )
{
	for( unsigned i = 0; i < members.size(); ++i )
		if( members[i].bPlaying )
			out.push_back( &members[i] );
	stable_sort( out.begin(), out.end(), BetterScore );
}

static CString ResultsText()
{
	CString s = ssprintf( "Round %d", ROOM->GetLastRound() );
	const vector<OnlineRoomResult> &results = ROOM->GetLastResults();
	for( unsigned i = 0; i < results.size(); ++i )
	{
		const OnlineRoomResult &r = results[i];
		s += ssprintf( "\n%d %s %s", r.iRank, r.sUser.c_str(), ScoreText(r.score).c_str() );
		if( !r.bFinished )
			s += " (left)";
	}
	return s;
}

void RoomOverlay::Rebuild()
{
	m_iSerial = ROOM->GetSerial();
	vector<Line> lines;

	if( !ROOM->CanUse() )
	{
		SetTexts( "Rooms", "", "Esc or F10: close", "Log in to play in rooms: F7 on the music select screen.", "" );
		SetLines( lines );
		SetEntryEnabled( false );
		return;
	}
	SetEntryEnabled( true );

	const vector<OnlineChatLine> &log = ROOM->GetLines();
	if( !ROOM->InRoom() )
	{
		CString sTitle = ROOM->IsJoining()? "Rooms: joining...": ROOM->IsListLoading()? "Rooms: loading...": "Rooms";
		const vector<OnlineRoomListing> &list = ROOM->GetList();
		for( unsigned i = 0; i < list.size(); ++i )
		{
			const OnlineRoomListing &l = list[i];
			CString s = ssprintf( "#%d  %s  %d/%d  host %s  %s", l.iID, l.sName.c_str(), l.iPlayers, l.iMaxPlayers,
				l.sHost.c_str(), l.sState.c_str() );
			if( l.chart.bSet )
				s += "  " + l.chart.Describe();
			if( l.bLocked )
				s += "  [password]";
			lines.push_back( Line(s, RageColor(1,1,1,1)) );
		}
		if( list.empty() && !ROOM->IsListLoading() && ROOM->GetListError().empty() )
			lines.push_back( Line("No rooms yet: /create one.", RageColor(0.7f,0.7f,0.7f,1)) );
		for( unsigned i = 0; i < log.size(); ++i )
			lines.push_back( FormatLine(log[i], ROOM->GetUser()) );
		SetTexts( sTitle, "", "/create [name]   /join <#> [password]   /list   /help   Esc or F10: close",
			ROOM->GetListError(), "" );
		SetLines( lines );
		return;
	}

	CString sTitle = ssprintf( "#%d %s%s  -  %s", ROOM->GetID(), ROOM->GetName().c_str(),
		ROOM->IsLocked()? " (password)": "", StateText().c_str() );
	CString sSub = "Chart: " + ROOM->GetChart().Describe();
	CString sStatus;
	if( ROOM->GetChart().bSet && !ROOM->HaveChart() )
		sStatus = ROOM->GetChartProblem();
	CString sHint = ROOM->IsHost()? "/pick  /start [force]  /ready  /leave  /help": "/ready  /unready  /leave  /help";

	/* Who's here: @ is the host, as on IRC. */
	CString sSide;
	const bool bRound = ROOM->GetState() != "lobby";
	const vector<OnlineRoomMember> &members = ROOM->GetMembers();
	for( unsigned i = 0; i < members.size(); ++i )
	{
		const OnlineRoomMember &m = members[i];
		CString s = (m.bHost? "@": "") + m.sUser;
		if( bRound )
		{
			if( !m.bPlaying )
				s += " -";
			else if( m.score.bSet )
				s += " " + ScoreText( m.score ) + (m.bDone? " done": "");
			else
				s += m.bLoaded? " playing": " loading";
		}
		else if( m.bReady )
			s += " ready";
		else if( ROOM->GetChart().bSet && !m.bHasChart )
			s += " no chart";
		sSide += s + "\n";
	}
	if( !bRound && ROOM->HasLastRound() )
		sSide += "\n" + ResultsText();

	for( unsigned i = 0; i < log.size(); ++i )
		lines.push_back( FormatLine(log[i], ROOM->GetUser()) );
	SetTexts( sTitle, sSub, sHint, sStatus, sSide );
	SetLines( lines );
}

/* What the room is doing, at the top of the screen while the panel is closed. */
void RoomOverlay::RebuildBar()
{
	m_bBarVisible = ROOM->InRoom();
	if( !m_bBarVisible )
		return;

	const Screen *pTop = SCREENMAN->GetTopScreen();
	const bool bGameplay = dynamic_cast<const ScreenGameplay *>( pTop ) != NULL;
	const bool bEval = dynamic_cast<const ScreenEvaluation *>( pTop ) != NULL;
	const CString sState = ROOM->GetState();

	CString s;
	if( bGameplay && ROOM->IsInRound() && sState == "loading" )
	{
		CString sWaiting;
		const vector<OnlineRoomMember> &members = ROOM->GetMembers();
		for( unsigned i = 0; i < members.size(); ++i )
			if( members[i].bPlaying && !members[i].bLoaded )
				sWaiting += (sWaiting.empty()? "": ", ") + members[i].sUser;
		s = "Waiting for " + (sWaiting.empty()? CString("the others"): sWaiting) + "...";
	}
	else if( (bGameplay || bEval) && sState != "lobby" )
	{
		/* Our own number is the one on screen, not the one the server last had. */
		vector<OnlineRoomMember> members = ROOM->GetMembers();
		for( unsigned i = 0; i < members.size(); ++i )
			if( bGameplay && members[i].sUser.CompareNoCase(ROOM->GetUser()) == 0 && members[i].bPlaying )
			{
				members[i].score.bSet = true;
				members[i].score.fWife = ROOM->GetLiveWife();
			}
		vector<const OnlineRoomMember *> v;
		Playing( members, v );
		s = ssprintf( "Round %d", ROOM->GetRound() );
		for( unsigned i = 0; i < v.size(); ++i )
			s += ssprintf( "\n%u %s %s%s", i+1, v[i]->sUser.c_str(), ScoreText(v[i]->score).c_str(), v[i]->bDone? " done": "" );
	}
	else if( bEval && ROOM->HasLastRound() )
		s = ResultsText();
	else
	{
		int iReady = 0;
		const vector<OnlineRoomMember> &members = ROOM->GetMembers();
		for( unsigned i = 0; i < members.size(); ++i )
			if( members[i].bReady || members[i].bHost )
				++iReady;
		s = ssprintf( "Room %s  -  %s  -  %d/%u ready  -  F10", ROOM->GetName().c_str(), StateText().c_str(),
			iReady, (unsigned) members.size() );
		if( sState == "loading" && ROOM->IsInRound() && dynamic_cast<const ScreenGameplay *>(pTop) == NULL )
			s += "\nThe round is starting: go to the music select screen!";
	}
	if( s == m_sBar )
		return;
	m_sBar = s;

	if( !m_bBarInit )
	{
		m_bBarInit = true;
		m_textBar.LoadFromFont( THEME->GetPathToF("Common normal") );
		m_textBar.SetHorizAlign( Actor::align_right );
		m_textBar.SetVertAlign( Actor::align_top );
		m_textBar.SetZoom( BAR_ZOOM );
		m_textBar.SetShadowLength( 0 );
		m_textBar.SetXY( BAR_RIGHT-6, BAR_TOP+4 );
		m_BarBack.SetDiffuse( RageColor(0,0,0,0.6f) );
	}
	m_textBar.SetText( s );
	m_BarBack.StretchTo( RectF(BAR_RIGHT - m_textBar.GetZoomedWidth() - 12, BAR_TOP,
		BAR_RIGHT, BAR_TOP + m_textBar.GetZoomedHeight() + 8) );
}

void RoomOverlay::Update( float fDeltaTime )
{
	UpdatePanel( fDeltaTime );
	RebuildBar();
	if( !IsOpen() )
		return;

	if( !ROOM->InRoom() && ROOM->CanUse() )
	{
		m_fListAge += fDeltaTime;
		if( m_fListAge >= LIST_REFRESH_SECONDS )
		{
			ROOM->RefreshList();
			m_fListAge = 0;
		}
	}
	if( m_iSerial != ROOM->GetSerial() )
		Rebuild();
}

void RoomOverlay::DrawClosed()
{
	if( !m_bBarVisible || !m_bBarInit )
		return;
	m_BarBack.Draw();
	m_textBar.Draw();
}

static void Help()
{
	ROOM->Notice( "/list: the rooms.  /create [name]: make one.  /join <#> [password]: join one.  /leave" );
	ROOM->Notice( "/ready, /unready: you have the chart and want to play it." );
	ROOM->Notice( "The host: /pick takes the chart and rate selected on the wheel; /start [force] starts the round "
		"(force leaves out who isn't ready); /kick <name>, /host <name>, /name <text>, /password [password]." );
	ROOM->Notice( "Anything else is said in the room; /me waves is an action." );
}

void RoomOverlay::Command( const CString &sCommand, const CString &sArgs )
{
	const bool bIn = ROOM->InRoom();
	if( sCommand == "help" )
		Help();
	else if( sCommand == "list" || sCommand == "rooms" )
	{
		ROOM->RefreshList();
		m_fListAge = 0;
		if( bIn )
			ROOM->Notice( "The room list shows once you /leave." );
	}
	else if( sCommand == "create" )
		ROOM->Create( sArgs, "" );
	else if( sCommand == "join" )
	{
		CString sNumber = sArgs, sPassword;
		const size_t iSpace = sArgs.find( ' ' );
		if( iSpace != CString::npos )
		{
			sNumber = sArgs.substr( 0, iSpace );
			sPassword = sArgs.substr( iSpace+1 );
		}
		TrimLeft( sNumber, "#" );
		const int iID = atoi( sNumber );
		if( iID <= 0 )
			ROOM->Notice( "Which room?  /join <#> [password], with the number from /list." );
		else
			ROOM->Join( iID, sPassword );
	}
	else if( !bIn )
		ROOM->Notice( "You aren't in a room: /list, then /join <#>, or /create." );
	else if( sCommand == "leave" || sCommand == "part" )
		ROOM->Leave();
	else if( sCommand == "ready" )
		ROOM->SetReady( true );
	else if( sCommand == "unready" )
		ROOM->SetReady( false );
	else if( sCommand == "pick" )
		ROOM->PickCurrent();
	else if( sCommand == "start" )
		ROOM->Start( sArgs.CompareNoCase("force") == 0 );
	else if( sCommand == "kick" && !sArgs.empty() )
		ROOM->Kick( sArgs );
	else if( sCommand == "host" && !sArgs.empty() )
		ROOM->GiveHost( sArgs );
	else if( sCommand == "name" && !sArgs.empty() )
		ROOM->Rename( sArgs );
	else if( sCommand == "password" )
		ROOM->SetPassword( sArgs );
	else
		ROOM->Notice( "Unknown command /" + sCommand + ": /help lists them." );
}

bool RoomOverlay::Submit( const CString &sText )
{
	if( sText[0] == '/' && sText.Left(4) != "/me " )
	{
		CString sCommand = sText.substr( 1 ), sArgs;
		const size_t iSpace = sCommand.find( ' ' );
		if( iSpace != CString::npos )
		{
			sArgs = sCommand.substr( iSpace+1 );
			sCommand = sCommand.substr( 0, iSpace );
			TrimLeft( sArgs );
			TrimRight( sArgs );
		}
		sCommand.MakeLower();
		Command( sCommand, sArgs );
		return true;
	}
	if( !ROOM->InRoom() )
	{
		ROOM->Notice( "You aren't in a room: /list, then /join <#>, or /create." );
		return false;
	}
	ROOM->Say( sText );
	return true;
}
