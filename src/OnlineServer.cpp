/* The parts of OnlineManager that talk to the server: the server's info,
 * accounts and their tokens, play tickets, the score queue, leaderboards and
 * the chat.  Every request is asynchronous (OnlineHttp); Update polls for the
 * answers. */
#include "global.h"
#include "OnlineManager.h"
#include "OnlineHttp.h"
#include "OnlineJson.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTimer.h"
#include "RageUtil.h"
#include "WifeManager.h"

#include <math.h>
#include <time.h>
#if !defined(WINDOWS)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const CString ONLINE_DIR = "Data/Online/";
static const CString QUEUE_DIR = "Data/Online/Queue/";
static const CString SUBMITTED_FILE = "Data/Online/Submitted.txt";
static const CString TOKEN_FILE = "OnlineToken.ini";

/* How long things fetched from the server are good for, in seconds. */
static const float BOARD_LIFETIME = 60;
static const float ME_LIFETIME = 60;
static const float INFO_RETRY = 30;
/* Retries of a queued score: 5 s, doubling, at most this far apart. */
static const int MAX_RETRY_DELAY = 300;

static float Now()
{
	return RageTimer::GetTimeSinceStart();
}

/* "2026-10-12T04:05:06Z" -> seconds since 1970, or 0. */
static time_t ParseRFC3339( const CString &s )
{
	int y, mo, d, h, mi, sec;
	if( sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6 )
		return 0;
	/* Days from 1970-01-01 to y-mo-d, for the proleptic Gregorian calendar. */
	y -= mo <= 2;
	const int era = (y >= 0? y: y - 399) / 400;
	const int yoe = y - era * 400;
	const int doy = (153 * (mo + (mo > 2? -3: 9)) + 2) / 5 + d - 1;
	const int doe = yoe * 365 + yoe/4 - yoe/100 + doy;
	const long days = era * 146097L + doe - 719468L;
	return (time_t) (days * 86400L + h * 3600L + mi * 60L + sec);
}

/* What went wrong with a request, for the player. */
static CString ErrorMessage( const OnlineHttpResponse &r, CString *pCode = NULL )
{
	if( pCode )
		*pCode = "";
	if( r.iStatus == 0 )
		return r.sError.empty()? CString("no answer from the server"): r.sError;
	JsonValue v;
	CString sError;
	if( JsonValue::Parse(r.sBody, v, sError) && v["error"].IsObject() )
	{
		if( pCode )
			*pCode = v["error"]["code"].GetString();
		const CString sMessage = v["error"]["message"].GetString();
		if( !sMessage.empty() )
			return sMessage;
	}
	return ssprintf( "the server answered %i", r.iStatus );
}

static bool ParseBody( const OnlineHttpResponse &r, JsonValue &v )
{
	CString sError;
	if( JsonValue::Parse(r.sBody, v, sError) && v.IsObject() )
		return true;
	LOG->Warn( "Online: the server's answer isn't JSON: %s", sError.c_str() );
	return false;
}

/* Only the path goes in the log; see OnlineHttp. */
static int Request( const CString &sServer, const CString &sMethod, const CString &sPath, const CString &sBody,
	const CString &sToken, int iTimeout = 30 )
{
	OnlineHttpRequest req;
	req.sMethod = sMethod;
	req.sURL = sServer + "/api/v1" + sPath;
	req.sBody = sBody;
	req.sToken = sToken;
	req.iTimeoutSeconds = iTimeout;
	req.bCompress = sBody.size() > 32*1024;
	return OnlineHttp::Send( req );
}

/* Writes a file only this user can read: it holds a login token. */
static bool WritePrivateFile( const CString &sPath, const CString &sData )
{
#if !defined(WINDOWS)
	CString sReal;
	if( FILEMAN->GetRealWritablePath(sPath, sReal) )
	{
		const CString sNew = sReal + ".new";
		unlink( sNew );
		const int fd = open( sNew, O_WRONLY|O_CREAT|O_TRUNC, 0600 );
		if( fd == -1 )
			return false;
		fchmod( fd, 0600 );
		const bool bOK = write( fd, sData.data(), sData.size() ) == (ssize_t) sData.size();
		if( close(fd) != 0 || !bOK || rename(sNew, sReal) != 0 )
		{
			unlink( sNew );
			return false;
		}
		FlushDirCache();
		return true;
	}
#endif
	/* Windows keeps %APPDATA% and the game folder to the user already. */
	RageFile f;
	if( !f.Open(sPath, RageFile::WRITE) )
		return false;
	return f.Write( sData ) != -1 && f.Flush() != -1;
}

/* Not DoesFileExist: the directory cache may not know a file written moments ago. */
static bool ReadFile( const CString &sPath, CString &sOut )
{
	sOut = "";
	RageFile f;
	if( !f.Open(sPath, RageFile::READ) )
		return false;
	return f.Read( sOut, f.GetFileSize() ) != -1;
}

/* "Key=Value" lines into a map. */
static void ParseKeyValues( const CString &s, std::map<CString,CString> &out )
{
	CStringArray asLines;
	split( s, "\n", asLines );
	for( unsigned i = 0; i < asLines.size(); ++i )
	{
		CString sLine = asLines[i];
		TrimRight( sLine );
		const size_t iEq = sLine.find( '=' );
		if( iEq == CString::npos )
			continue;
		out[sLine.substr(0, iEq)] = sLine.substr( iEq + 1 );
	}
}

bool OnlineManager::IsBuiltIn() const
{
	return OnlineHttp::IsBuiltIn();
}

CString OnlineManager::GetServerProblem() const
{
	if( !IsBuiltIn() )
		return "This build of the game has no online support.";
	if( !m_bServerOK )
		return "OnlineServer " + OnlineHttp::CheckURL( m_sServer, PREFSMAN->m_bOnlineAllowInsecureHttp ) + ".";
	if( m_InfoState == INFO_WRONG_API )
		return "The server speaks another version of the online API.";
	if( m_InfoState == INFO_FAILED )
		return "Can't reach the server: " + m_sInfoError;
	return "";
}

/* Notice a change of OnlineServer, and ask a new server who it is. */
void OnlineManager::CheckServer()
{
	CString sServer = PREFSMAN->m_sOnlineServer;
	TrimLeft( sServer );
	TrimRight( sServer );
	while( sServer.size() && sServer[sServer.size()-1] == '/' )
		sServer.erase( sServer.size()-1 );

	if( sServer != m_sServer )
	{
		m_sServer = sServer;
		m_bServerOK = !sServer.empty() && OnlineHttp::CheckURL( sServer, PREFSMAN->m_bOnlineAllowInsecureHttp ).empty();
		if( m_iInfoRequest )
			OnlineHttp::Forget( m_iInfoRequest );
		m_iInfoRequest = 0;
		m_InfoState = INFO_NONE;
		m_sServerName = m_sInfoError = m_sClockWarning = "";
		FOREACH_PlayerNumber( pn )
			m_Account[pn] = Account();
		m_Boards.clear();
		m_bQueueLoaded = false;
		if( !sServer.empty() )
			LOG->Info( "Online: server %s%s", sServer.c_str(), m_bServerOK? "": " (not usable)" );
	}

	if( !IsEnabled() || !m_bServerOK || !IsBuiltIn() )
		return;

	if( m_iInfoRequest )
	{
		OnlineHttpResponse r;
		if( !OnlineHttp::Poll(m_iInfoRequest, r) )
			return;
		m_iInfoRequest = 0;
		JsonValue v;
		if( r.iStatus != 200 || !ParseBody(r, v) )
		{
			m_InfoState = INFO_FAILED;
			m_sInfoError = ErrorMessage( r );
			m_fInfoRetry = Now() + INFO_RETRY;
			return;
		}
		if( v["api_version"].GetInt() != 1 )
		{
			m_InfoState = INFO_WRONG_API;
			LOG->Warn( "Online: the server's API version is %i, not 1", v["api_version"].GetInt() );
			return;
		}
		m_InfoState = INFO_OK;
		m_sServerName = v["name"].GetString();
		const time_t tServer = ParseRFC3339( v["time"].GetString() );
		const long iSkew = (long) (time(NULL) - tServer);
		if( tServer && (iSkew > 300 || iSkew < -300) )
			m_sClockWarning = ssprintf( "This computer's clock is %li minutes off the server's.", labs(iSkew) / 60 );
		LOG->Info( "Online: connected to \"%s\" (server %s)", m_sServerName.c_str(), v["server_version"].GetString().c_str() );
		return;
	}

	if( m_InfoState == INFO_NONE || (m_InfoState == INFO_FAILED && Now() > m_fInfoRetry) )
	{
		m_InfoState = INFO_ASKING;
		m_iInfoRequest = Request( m_sServer, "GET", "/info", "", "" );
	}
}

/*
 * Accounts
 */

CString OnlineManager::GetAccountFile( PlayerNumber pn ) const
{
	if( PROFILEMAN->IsUsingProfile(pn) )
		return PROFILEMAN->GetProfileDir( (ProfileSlot) pn ) + TOKEN_FILE;
	return ONLINE_DIR + ssprintf( "GuestP%i.ini", pn+1 );
}

/* The token in sFile if it is for this server and hasn't expired. */
CString OnlineManager::ReadToken( const CString &sFile ) const
{
	CString s;
	if( !ReadFile(sFile, s) )
		return "";
	std::map<CString,CString> kv;
	ParseKeyValues( s, kv );
	if( kv["Server"] != m_sServer )
		return "";
	const time_t tExpires = ParseRFC3339( kv["ExpiresAt"] );
	if( tExpires && tExpires < time(NULL) )
		return "";
	return kv["Token"];
}

void OnlineManager::LoadAccount( PlayerNumber pn )
{
	Account &a = m_Account[pn];
	a = Account();
	a.sFile = GetAccountFile( pn );
	CString s;
	if( !ReadFile(a.sFile, s) )
		return;
	std::map<CString,CString> kv;
	ParseKeyValues( s, kv );
	if( kv["Server"] != m_sServer )
		return;
	a.sToken = ReadToken( a.sFile );
	if( a.sToken.empty() )
	{
		a.info.sStatus = "Your login has expired.  Log in again.";
		return;
	}
	a.info.bLoggedIn = true;
	a.info.sUsername = kv["Username"];
}

void OnlineManager::ForgetToken( PlayerNumber pn, const CString &sWhy )
{
	Account &a = m_Account[pn];
	if( !a.sFile.empty() && DoesFileExist(a.sFile) )
	{
		FILEMAN->Remove( a.sFile );
		FlushDirCache();
	}
	a.sToken = "";
	a.info.bLoggedIn = false;
	a.info.bHasRating = false;
	a.info.sStatus = sWhy;
}

const OnlineAccount &OnlineManager::GetAccount( PlayerNumber pn )
{
	if( m_Account[pn].sFile != GetAccountFile(pn) )
		LoadAccount( pn );
	return m_Account[pn].info;
}

void OnlineManager::Login( PlayerNumber pn, const CString &sUsername, const CString &sPassword )
{
	Account &a = m_Account[pn];
	if( a.info.bBusy || !IsEnabled() )
		return;
	if( !GetServerProblem().empty() && m_InfoState != INFO_FAILED )
	{
		a.info.sStatus = GetServerProblem();
		return;
	}

	JsonValue v = JsonValue::Object();
	v.Set( "username", JsonValue::String(sUsername) );
	v.Set( "password", JsonValue::String(sPassword) );
	v.Set( "client", ClientInfo() );
	a.iLoginRequest = Request( m_sServer, "POST", "/auth/login", v.Write(), "" );
	a.info.bBusy = true;
	a.info.sStatus = "Logging in...";
	a.info.sUsername = sUsername;
}

void OnlineManager::Logout( PlayerNumber pn )
{
	Account &a = m_Account[pn];
	if( a.info.bBusy || !a.info.bLoggedIn )
		return;
	/* Revoke the token on the server, but forget it here whatever happens. */
	a.iLogoutRequest = Request( m_sServer, "POST", "/auth/logout", "", a.sToken );
	ForgetToken( pn, "Logged out." );
	a.info.sUsername = "";
}

void OnlineManager::RefreshAccount( PlayerNumber pn )
{
	Account &a = m_Account[pn];
	if( !a.info.bLoggedIn || a.iMeRequest )
		return;
	if( a.fMeFetched >= 0 && Now() - a.fMeFetched < ME_LIFETIME )
		return;
	a.iMeRequest = Request( m_sServer, "GET", "/me", "", a.sToken );
}

static void ReadRating( const JsonValue &v, float out[MINACALC_NUM_SKILLSETS] )
{
	static const char *szNames[MINACALC_NUM_SKILLSETS] =
		{ "overall", "stream", "jumpstream", "handstream", "stamina", "jackspeed", "chordjack", "technical" };
	for( int i = 0; i < MINACALC_NUM_SKILLSETS; ++i )
		out[i] = (float) v[szNames[i]].GetNumber();
}

void OnlineManager::UpdateAccounts()
{
	FOREACH_PlayerNumber( pn )
	{
		Account &a = m_Account[pn];
		OnlineHttpResponse r;

		if( a.iLogoutRequest && OnlineHttp::Poll(a.iLogoutRequest, r) )
			a.iLogoutRequest = 0;

		if( a.iLoginRequest && OnlineHttp::Poll(a.iLoginRequest, r) )
		{
			a.iLoginRequest = 0;
			a.info.bBusy = false;
			JsonValue v;
			if( r.iStatus != 200 || !ParseBody(r, v) || v["token"].GetString().empty() )
			{
				CString sCode;
				a.info.sStatus = ErrorMessage( r, &sCode );
				if( sCode == "bad_credentials" )
					a.info.sStatus = "Wrong username or password.";
				LOG->Info( "Online: P%i couldn't log in: %s", pn+1, a.info.sStatus.c_str() );
				continue;
			}

			a.sFile = GetAccountFile( pn );
			a.sToken = v["token"].GetString();
			a.info.bLoggedIn = true;
			a.info.sUsername = v["user"]["username"].GetString( a.info.sUsername );
			a.info.sStatus = "Logged in as " + a.info.sUsername + ".";
			a.fMeFetched = -1;

			const CString sFile = "[Online]\n"
				"Server=" + m_sServer + "\n"
				"Username=" + a.info.sUsername + "\n"
				"Token=" + a.sToken + "\n"
				"ExpiresAt=" + v["expires_at"].GetString() + "\n";
			if( !WritePrivateFile(a.sFile, sFile) )
				LOG->Warn( "Online: couldn't save the login in \"%s\"", a.sFile.c_str() );
			LOG->Info( "Online: P%i logged in as %s", pn+1, a.info.sUsername.c_str() );
			RefreshAccount( pn );
			m_Boards.clear();	// they show the player's own entry
		}

		if( a.iMeRequest && OnlineHttp::Poll(a.iMeRequest, r) )
		{
			a.iMeRequest = 0;
			a.fMeFetched = Now();
			JsonValue v;
			if( r.iStatus == 401 )
				ForgetToken( pn, "Your login has expired.  Log in again." );
			else if( r.iStatus == 200 && ParseBody(r, v) )
			{
				a.info.bHasRating = v["rating"].IsObject();
				ReadRating( v["rating"], a.info.fRating );
				a.info.iRank = v["ranks"]["overall"].GetInt( 0 );
			}
		}
	}
}

/*
 * Leaderboards
 */

const OnlineLeaderboard &OnlineManager::GetLeaderboard( PlayerNumber pn, const CString &sChartKey, float fRate )
{
	const Account &a = m_Account[pn];
	const CString sRate = ssprintf( "%.2f", fRate );
	const CString sKey = sChartKey + "@" + sRate + "#" + a.info.sUsername;
	std::map<CString, Board>::iterator it = m_Boards.find( sKey );
	if( it == m_Boards.end() )
	{
		/* Browsing the wheel with the panel up adds a board per chart; don't keep them all. */
		if( m_Boards.size() > 200 )
		{
			for( it = m_Boards.begin(); it != m_Boards.end(); ++it )
				if( it->second.iRequest )
					OnlineHttp::Forget( it->second.iRequest );
			m_Boards.clear();
		}
		Board b;
		b.iRequest = 0;
		b.fFetched = -1;
		it = m_Boards.insert( std::make_pair(sKey, b) ).first;
	}
	Board &b = it->second;
	if( b.iRequest == 0 && (b.fFetched < 0 || Now() - b.fFetched > BOARD_LIFETIME) && GetServerProblem().empty() )
	{
		b.iRequest = Request( m_sServer, "GET", "/charts/" + sChartKey + "/leaderboard?rate=" + sRate + "&limit=10", "", a.sToken );
		b.lb.bLoading = true;
	}
	return b.lb;
}

static void ReadEntry( const JsonValue &v, OnlineLeaderboardEntry &e )
{
	const JsonValue &s = v["score"];
	e.iRank = v["rank"].GetInt();
	e.sUsername = s["user"]["username"].GetString();
	e.fWife = (float) s["wife"].GetNumber();
	e.fWifeJ4 = (float) s["wife_j4"].GetNumber();
	e.fJudgeScale = (float) s["judge_scale"].GetNumber( 1 );
	e.fSSROverall = (float) s["ssr"]["overall"].GetNumber();
	e.sStatus = s["status"].GetString();
	e.sDate = s["played_at"].GetString().Left( 10 );
}

void OnlineManager::UpdateBoards()
{
	for( std::map<CString, Board>::iterator it = m_Boards.begin(); it != m_Boards.end(); ++it )
	{
		Board &b = it->second;
		OnlineHttpResponse r;
		if( b.iRequest == 0 || !OnlineHttp::Poll(b.iRequest, r) )
			continue;
		b.iRequest = 0;
		b.fFetched = Now();
		b.lb = OnlineLeaderboard();
		JsonValue v;
		if( r.iStatus == 404 )
			b.lb.sError = "This chart isn't on the server.";
		else if( r.iStatus != 200 || !ParseBody(r, v) )
			b.lb.sError = ErrorMessage( r );
		else
		{
			b.lb.iTotal = v["total"].GetInt();
			const JsonValue &e = v["entries"];
			for( unsigned i = 0; i < e.size(); ++i )
			{
				OnlineLeaderboardEntry entry;
				ReadEntry( e[i], entry );
				b.lb.vEntries.push_back( entry );
			}
			b.lb.bHasMe = v["me"].IsObject();
			if( b.lb.bHasMe )
				ReadEntry( v["me"], b.lb.me );
		}
	}
}

/*
 * Play tickets
 */

void OnlineManager::RequestTicket( PlayerNumber pn )
{
	PlayData &p = m_Play[pn];
	const Account &a = m_Account[pn];
	p.sAccountFile = a.sFile;

	JsonValue v = JsonValue::Object();
	v.Set( "chart_key", JsonValue::String(p.sChartKey) );
	v.Set( "rate", RateValue(p.fRate) );
	v.Set( "judge_scale", JsonValue::Float(p.fJudgeScale) );
	v.Set( "player_options", JsonValue::String(p.sPlayerOptions) );
	v.Set( "song_options", JsonValue::String(p.sSongOptions) );
	v.Set( "client", ClientInfo() );
	p.iTicketRequest = Request( m_sServer, "POST", "/tickets", v.Write(), a.sToken, 10 );
}

void OnlineManager::UpdateTickets()
{
	FOREACH_PlayerNumber( pn )
	{
		PlayData &p = m_Play[pn];
		OnlineHttpResponse r;
		if( p.iTicketRequest && OnlineHttp::Poll(p.iTicketRequest, r) )
		{
			p.iTicketRequest = 0;
			JsonValue v;
			CString sCode;
			if( (r.iStatus == 200 || r.iStatus == 201) && ParseBody(r, v) && !v["ticket"].GetString().empty() )
			{
				p.sTicket = v["ticket"].GetString();
				/* Our clock may be off the server's: count from when it arrived. */
				const time_t tIssued = ParseRFC3339( v["issued_at"].GetString() );
				const time_t tEarliest = ParseRFC3339( v["earliest_submit_at"].GetString() );
				const time_t tExpires = ParseRFC3339( v["expires_at"].GetString() );
				const time_t tNow = time(NULL);
				p.tSubmitFrom = tNow + max( (long) (tEarliest - tIssued), 0L ) + 1;
				p.tTicketExpires = tNow + max( (long) (tExpires - tIssued), 0L );
				LOG->Info( "Online: P%i has a play ticket", pn+1 );
			}
			else if( r.iStatus == 422 )
				Unranked( pn, ErrorMessage(r) );
			else if( r.iStatus == 401 )
			{
				ForgetToken( pn, "Your login has expired.  Log in again." );
				p.sTicketError = "not logged in";
			}
			else
				p.sTicketError = ErrorMessage( r );
			if( !p.sTicketError.empty() )
				LOG->Info( "Online: P%i got no play ticket: %s", pn+1, p.sTicketError.c_str() );
		}

		/* The results screen is waiting for the ticket. */
		if( p.bWantSubmit && p.iTicketRequest == 0 )
			FinishStage( pn );
	}
}

/* The play is over and its ticket is in, or isn't coming: send it, or say why not. */
void OnlineManager::FinishStage( PlayerNumber pn )
{
	PlayData &p = m_Play[pn];
	p.bWantSubmit = false;
	if( !p.sUnranked.empty() )
		SetResult( pn, m_Result[pn].sScoreKey, OnlineStageResult::UNRANKED, p.sUnranked );
	else if( p.sTicket.empty() )
		SetResult( pn, m_Result[pn].sScoreKey, OnlineStageResult::UNRANKED, "no ticket (" + p.sTicketError + ")" );
	else if( !PREFSMAN->m_bOnlineAutoSubmit )
		SetResult( pn, m_Result[pn].sScoreKey, OnlineStageResult::NOT_SENT, "" );
	else
		SubmitStage( pn );
}

/*
 * The score queue
 */

void OnlineManager::SetResult( PlayerNumber pn, const CString &sScoreKey, OnlineStageResult::State state, const CString &sReason )
{
	if( pn == PLAYER_INVALID )
		return;
	OnlineStageResult &r = m_Result[pn];
	if( !r.bValid || r.sScoreKey != sScoreKey )
		return;
	r.state = state;
	r.sReason = sReason;
}

void OnlineManager::SubmitStage( PlayerNumber pn )
{
	PlayData &p = m_Play[pn];
	if( p.sBody.empty() || p.sTicket.empty() )
		return;
	/* The ticket goes first; the rest was made at the end of the song. */
	const CString sBody = "{\"ticket\":" + JsonQuote( p.sTicket ) + "," + p.sBody.substr( 1 );
	p.sBody = "";
	Enqueue( pn, sBody );
}

void OnlineManager::Submitted( const CString &sScoreKey )
{
	if( m_Submitted.count(sScoreKey) )
		return;
	m_Submitted.insert( sScoreKey );
	CString s;
	for( std::set<CString>::const_iterator it = m_Submitted.begin(); it != m_Submitted.end(); ++it )
		s += *it + "\n";
	RageFile f;
	if( !f.Open(SUBMITTED_FILE, RageFile::WRITE) || f.Write(s) == -1 || f.Flush() == -1 )
		LOG->Warn( "Online: couldn't write %s", SUBMITTED_FILE.c_str() );
}

void OnlineManager::SaveQueued( const Queued &q ) const
{
	const CString s = "[Queued]\n"
		"Server=" + q.sServer + "\n"
		"Account=" + q.sAccountFile + "\n" +
		ssprintf( "NotBefore=%li\nExpires=%li\nTries=%i\n", (long) q.tNotBefore, (long) q.tExpires, q.iTries );
	RageFile f;
	if( !f.Open(QUEUE_DIR + q.sScoreKey + ".ini", RageFile::WRITE) || f.Write(s) == -1 || f.Flush() == -1 )
		LOG->Warn( "Online: couldn't save the queue entry of %s", q.sScoreKey.c_str() );
}

void OnlineManager::Enqueue( PlayerNumber pn, const CString &sBody )
{
	const PlayData &p = m_Play[pn];
	Queued q;
	q.sScoreKey = m_Result[pn].sScoreKey;
	q.sServer = m_sServer;
	q.sAccountFile = p.sAccountFile;
	q.tNotBefore = p.tSubmitFrom;
	q.tExpires = p.tTicketExpires;
	q.pn = pn;
	if( m_Submitted.count(q.sScoreKey) )
		return;

	RageFile f;
	if( !f.Open(QUEUE_DIR + q.sScoreKey + ".json", RageFile::WRITE) || f.Write(sBody) == -1 || f.Flush() == -1 )
		LOG->Warn( "Online: couldn't save the score %s for sending later", q.sScoreKey.c_str() );
	f.Close();
	SaveQueued( q );
	FlushDirCache();
	q.sBody = sBody;
	m_Queue.push_back( q );
	SetResult( pn, q.sScoreKey, time(NULL) < q.tNotBefore? OnlineStageResult::WAITING: OnlineStageResult::SENDING, "" );
}

void OnlineManager::RemoveQueued( unsigned i )
{
	const Queued &q = m_Queue[i];
	if( q.iRequest )
		OnlineHttp::Forget( q.iRequest );
	FILEMAN->Remove( QUEUE_DIR + q.sScoreKey + ".json" );
	FILEMAN->Remove( QUEUE_DIR + q.sScoreKey + ".ini" );
	FlushDirCache();
	m_Queue.erase( m_Queue.begin() + i );
}

void OnlineManager::LoadQueue()
{
	m_bQueueLoaded = true;

	m_Submitted.clear();
	CString s;
	if( ReadFile(SUBMITTED_FILE, s) )
	{
		CStringArray as;
		split( s, "\n", as );
		for( unsigned i = 0; i < as.size(); ++i )
		{
			TrimRight( as[i] );
			if( !as[i].empty() )
				m_Submitted.insert( as[i] );
		}
	}

	/* Keep what this session has queued; add what earlier ones left. */
	CStringArray asFiles;
	GetDirListing( QUEUE_DIR + "*.ini", asFiles, false, false );
	for( unsigned i = 0; i < asFiles.size(); ++i )
	{
		const CString sKey = asFiles[i].Left( asFiles[i].size() - 4 );
		bool bHave = false;
		for( unsigned j = 0; j < m_Queue.size(); ++j )
			if( m_Queue[j].sScoreKey == sKey )
				bHave = true;
		if( bHave || !ReadFile(QUEUE_DIR + asFiles[i], s) )
			continue;
		std::map<CString,CString> kv;
		ParseKeyValues( s, kv );
		Queued q;
		q.sScoreKey = sKey;
		q.sServer = kv["Server"];
		q.sAccountFile = kv["Account"];
		q.tNotBefore = (time_t) atol( kv["NotBefore"] );
		q.tExpires = (time_t) atol( kv["Expires"] );
		q.iTries = atoi( kv["Tries"] );
		m_Queue.push_back( q );
	}
	if( !m_Queue.empty() )
		LOG->Info( "Online: %u scores waiting to be sent", (unsigned) m_Queue.size() );
}

void OnlineManager::FetchRank( PlayerNumber pn )
{
	const OnlineStageResult &r = m_Result[pn];
	const WifeStageResult &wr = WIFE->GetStageResult( pn );
	if( m_iRankRequest[pn] || !r.bValid || wr.sChartKey.empty() )
		return;
	m_iRankRequest[pn] = Request( m_sServer, "GET", "/charts/" + wr.sChartKey + "/leaderboard?rate=" + ssprintf("%.2f", r.fRate) + "&limit=1",
		"", ReadToken(m_Account[pn].sFile) );
}

void OnlineManager::UpdateQueue()
{
	if( !m_bQueueLoaded )
		LoadQueue();

	FOREACH_PlayerNumber( pn )
	{
		OnlineHttpResponse r;
		if( m_iRankRequest[pn] && OnlineHttp::Poll(m_iRankRequest[pn], r) )
		{
			m_iRankRequest[pn] = 0;
			JsonValue v;
			if( r.iStatus == 200 && ParseBody(r, v) && v["me"].IsObject() )
				m_Result[pn].iRank = v["me"]["rank"].GetInt();
		}
	}

	const time_t tNow = time(NULL);
	for( unsigned i = 0; i < m_Queue.size(); ++i )
	{
		Queued &q = m_Queue[i];
		if( q.sServer != m_sServer )
			continue;
		if( m_Submitted.count(q.sScoreKey) )
		{
			RemoveQueued( i-- );
			continue;
		}

		if( q.iRequest == 0 )
		{
			if( tNow > q.tExpires + 60 )
			{
				LOG->Info( "Online: dropping %s: its ticket expired", q.sScoreKey.c_str() );
				SetResult( q.pn, q.sScoreKey, OnlineStageResult::REJECTED, "the play ticket expired before the score could be sent" );
				RemoveQueued( i-- );
				continue;
			}
			if( tNow < q.tNotBefore || Now() < q.fNextTry )
				continue;
			const CString sToken = ReadToken( q.sAccountFile );
			if( sToken.empty() )
			{
				SetResult( q.pn, q.sScoreKey, OnlineStageResult::QUEUED, "log in again to send it" );
				q.fNextTry = Now() + 5;
				continue;
			}
			if( q.sBody.empty() && (!ReadFile(QUEUE_DIR + q.sScoreKey + ".json", q.sBody) || q.sBody.empty()) )
			{
				LOG->Warn( "Online: couldn't read the saved score %s", q.sScoreKey.c_str() );
				SetResult( q.pn, q.sScoreKey, OnlineStageResult::REJECTED, "the saved score couldn't be read" );
				RemoveQueued( i-- );
				continue;
			}
			q.iRequest = Request( m_sServer, "POST", "/scores", q.sBody, sToken, 60 );
			SetResult( q.pn, q.sScoreKey, OnlineStageResult::SENDING, "" );
			continue;
		}

		OnlineHttpResponse r;
		if( !OnlineHttp::Poll(q.iRequest, r) )
			continue;
		q.iRequest = 0;

		CString sCode;
		const CString sMessage = ErrorMessage( r, &sCode );
		JsonValue v;
		if( (r.iStatus == 200 || r.iStatus == 201) && ParseBody(r, v) )
		{
			const CString sStatus = v["score"]["status"].GetString();
			LOG->Info( "Online: %s sent, %s", q.sScoreKey.c_str(), sStatus.c_str() );
			Submitted( q.sScoreKey );
			if( q.pn != PLAYER_INVALID && m_Result[q.pn].sScoreKey == q.sScoreKey )
			{
				OnlineStageResult &res = m_Result[q.pn];
				res.fRatingBefore = (float) v["rating_before"]["overall"].GetNumber();
				res.fRatingAfter = (float) v["rating_after"]["overall"].GetNumber();
				CString sReasons;
				const JsonValue &reasons = v["score"]["status_reasons"];
				for( unsigned j = 0; j < reasons.size(); ++j )
					sReasons += (j? ", ": "") + reasons[j].GetString();
				SetResult( q.pn, q.sScoreKey, sStatus == "accepted"? OnlineStageResult::ACCEPTED: OnlineStageResult::PENDING, sReasons );
				m_Account[q.pn].fMeFetched = -1;
				RefreshAccount( q.pn );
				FetchRank( q.pn );
			}
			m_Boards.clear();
			RemoveQueued( i-- );
		}
		else if( r.iStatus == 409 && sCode == "duplicate_score" )
		{
			/* It's there already (or someone else's key, which can't happen honestly). */
			Submitted( q.sScoreKey );
			SetResult( q.pn, q.sScoreKey, OnlineStageResult::REJECTED, sMessage );
			RemoveQueued( i-- );
		}
		else if( r.iStatus == 401 )
		{
			/* Keep it; it goes once the player logs in again. */
			bool bForgot = false;
			FOREACH_PlayerNumber( pn )
				if( m_Account[pn].sFile == q.sAccountFile )
				{
					ForgetToken( pn, "Your login has expired.  Log in again." );
					bForgot = true;
				}
			if( !bForgot && DoesFileExist(q.sAccountFile) )
			{
				FILEMAN->Remove( q.sAccountFile );
				FlushDirCache();
			}
			SetResult( q.pn, q.sScoreKey, OnlineStageResult::QUEUED, "log in again to send it" );
		}
		else if( r.iStatus == 0 || r.iStatus == 429 || r.iStatus >= 500 ||
			(r.iStatus == 403 && sCode == "forbidden") )
		{
			/* Not now: try again later.  (403 forbidden is an account too new to submit.) */
			int iDelay = min( 5 << min(q.iTries, 10), MAX_RETRY_DELAY );
			if( r.iRetryAfter > iDelay )
				iDelay = min( r.iRetryAfter, 3600 );
			++q.iTries;
			q.fNextTry = Now() + iDelay;
			SaveQueued( q );
			SetResult( q.pn, q.sScoreKey, OnlineStageResult::QUEUED, sMessage );
			LOG->Info( "Online: %s not sent (%s), trying again in %i s", q.sScoreKey.c_str(), sMessage.c_str(), iDelay );
		}
		else
		{
			LOG->Info( "Online: %s refused: %s %s", q.sScoreKey.c_str(), sCode.c_str(), sMessage.c_str() );
			SetResult( q.pn, q.sScoreKey, OnlineStageResult::REJECTED, sMessage );
			RemoveQueued( i-- );
		}
	}
}

/*
 * The chat
 */

/* Seconds between polls, and how many lines are kept. */
static const float CHAT_POLL_OPEN = 2;
static const float CHAT_POLL_CLOSED = 10;
static const unsigned CHAT_KEEP = 100;

bool OnlineManager::CanChat()
{
	FOREACH_PlayerNumber( pn )
	{
		const OnlineAccount &a = GetAccount( pn );
		if( a.bLoggedIn && !a.bBusy )
			return true;
	}
	return false;
}

void OnlineManager::SetChatOpen( bool bOpen )
{
	if( bOpen && !m_bChatOpen )
		m_fChatNextPoll = 0;	// catch up at once
	m_bChatOpen = bOpen;
}

bool OnlineManager::ChatSay( const CString &sText )
{
	if( m_ChatPlayer == PLAYER_INVALID || m_iChatSayRequest )
		return false;
	JsonValue v = JsonValue::Object();
	v.Set( "text", JsonValue::String(sText) );
	if( m_iChatLastID >= 0 )
		v.Set( "after", JsonValue::Int(m_iChatLastID) );
	m_iChatSayRequest = Request( m_sServer, "POST", "/chat", v.Write(), m_sChatToken, 10 );
	return true;
}

void OnlineManager::ReadChat( const OnlineHttpResponse &r, PlayerNumber pn )
{
	JsonValue v;
	CString sCode;
	if( (r.iStatus == 200 || r.iStatus == 201) && ParseBody(r, v) )
	{
		const JsonValue &msgs = v["messages"];
		for( unsigned i = 0; i < msgs.size(); ++i )
		{
			const JsonValue &m = msgs[i];
			OnlineChatLine line;
			line.iID = m["id"].GetInt();
			if( line.iID <= m_iChatLastID )
				continue;	// a poll and a post crossed
			const CString sKind = m["kind"].GetString();
			line.kind = sKind == "action"? OnlineChatLine::ACTION:
				sKind == "join"? OnlineChatLine::JOIN:
				sKind == "part"? OnlineChatLine::PART: OnlineChatLine::MESSAGE;
			line.sUser = m["user"]["username"].GetString();
			line.sText = m["text"].GetString();
			line.tAt = ParseRFC3339( m["at"].GetString() );
			m_ChatLines.push_back( line );
			m_iChatLastID = line.iID;
		}
		if( m_ChatLines.size() > CHAT_KEEP )
			m_ChatLines.erase( m_ChatLines.begin(), m_ChatLines.end() - CHAT_KEEP );
		/* With nothing new, it's whatever the server has. */
		m_iChatLastID = max( m_iChatLastID, v["last_id"].GetInt(m_iChatLastID) );

		m_ChatOnline.clear();
		const JsonValue &online = v["online"];
		for( unsigned i = 0; i < online.size(); ++i )
			m_ChatOnline.push_back( online[i]["username"].GetString() );
		m_sChatStatus = "";
	}
	else if( r.iStatus == 401 )
		ForgetToken( pn, "Your login has expired.  Log in again." );
	else
	{
		m_sChatStatus = ErrorMessage( r, &sCode );
		if( r.iRetryAfter > 0 )
			m_fChatNextPoll = Now() + min( r.iRetryAfter, 300 );
	}
	++m_iChatSerial;
}

void OnlineManager::UpdateChat()
{
	/* The first player who is logged in chats; if that changes, start over. */
	PlayerNumber chatter = PLAYER_INVALID;
	FOREACH_PlayerNumber( pn )
	{
		const OnlineAccount &a = GetAccount( pn );
		if( a.bLoggedIn && !a.bBusy )
		{
			chatter = pn;
			break;
		}
	}
	const CString sToken = chatter == PLAYER_INVALID? CString(): m_Account[chatter].sToken;
	if( sToken != m_sChatToken )
	{
		if( m_iChatPollRequest )
			OnlineHttp::Forget( m_iChatPollRequest );
		if( m_iChatSayRequest )
			OnlineHttp::Forget( m_iChatSayRequest );
		m_iChatPollRequest = m_iChatSayRequest = 0;
		m_ChatPlayer = chatter;
		m_sChatToken = sToken;
		m_sChatUser = chatter == PLAYER_INVALID? CString(): m_Account[chatter].info.sUsername;
		m_ChatLines.clear();
		m_ChatOnline.clear();
		m_sChatStatus = "";
		m_iChatLastID = -1;
		m_fChatNextPoll = 0;
		++m_iChatSerial;
	}
	if( m_ChatPlayer == PLAYER_INVALID )
		return;

	OnlineHttpResponse r;
	if( m_iChatPollRequest && OnlineHttp::Poll(m_iChatPollRequest, r) )
	{
		m_iChatPollRequest = 0;
		ReadChat( r, m_ChatPlayer );
	}
	if( m_iChatSayRequest && OnlineHttp::Poll(m_iChatSayRequest, r) )
	{
		m_iChatSayRequest = 0;
		ReadChat( r, m_ChatPlayer );
	}

	if( m_iChatPollRequest == 0 && m_sChatToken == m_Account[m_ChatPlayer].sToken && Now() >= m_fChatNextPoll )
	{
		CString sPath = "/chat";
		if( m_iChatLastID >= 0 )
			sPath += ssprintf( "?after=%i", m_iChatLastID );
		m_iChatPollRequest = Request( m_sServer, "GET", sPath, "", m_sChatToken, 10 );
		m_fChatNextPoll = Now() + (m_bChatOpen? CHAT_POLL_OPEN: CHAT_POLL_CLOSED);
	}
}
