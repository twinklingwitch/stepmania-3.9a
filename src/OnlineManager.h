/* OnlineManager - Online scores: accounts, play tickets, score submission
 * with a retry queue, online leaderboards and ratings, and the raw input log
 * of each play that the server judges.  The protocol is docs/API.md in the
 * server's repository.
 *
 * Everything here hangs off Wife scoring: a play is only recorded while Wife is
 * active for the player, and only if a server is configured (OnlineServer) or
 * test vectors are wanted (OnlineWriteTestVectors).  With no server nothing
 * goes over the network, nothing is written, and every call in from the game
 * returns at once.  Network requests never block: see OnlineHttp. */

#ifndef ONLINE_MANAGER_H
#define ONLINE_MANAGER_H

#include "PlayerNumber.h"
#include "GameConstantsAndTypes.h"
#include "OnlineInputLog.h"
#include "OnlineRules.h"
#include "minacalc_c.h"

#include <map>
#include <set>
#include <time.h>

class JsonValue;
class NoteData;
struct OnlineHttpResponse;
class NoteDataWithScoring;
class Song;
class Steps;
struct HighScore;

/* What the results screen shows about a play. */
struct OnlineStageResult
{
	bool bValid;
	float fWifeJ4;
	float fRate;
	bool bHasSSR;
	float fSSR[MINACALC_NUM_SKILLSETS];	// at Judge 4

	enum State
	{
		NOT_ONLINE,		// no server
		UNRANKED,		// sReason says why
		NOT_SENT,		// OnlineAutoSubmit is off: F7 on the results screen sends it
		WAITING,		// for the ticket, or until the song's length has passed
		SENDING,
		QUEUED,			// couldn't send; it's tried again later
		ACCEPTED,
		PENDING,		// accepted for review by a moderator
		REJECTED		// sReason has the server's reason
	} state;
	CString sReason;
	CString sScoreKey;
	int iRank;			// on the chart's leaderboard at this rate, once known
	float fRatingBefore, fRatingAfter;	// overall
	OnlineStageResult(): bValid(false), fWifeJ4(0), fRate(1), bHasSSR(false), state(NOT_ONLINE), iRank(0), fRatingBefore(0), fRatingAfter(0) { }
};

struct OnlineAccount
{
	bool bLoggedIn;
	CString sUsername;
	CString sStatus;	// what's going on, or what went wrong last
	bool bBusy;			// logging in or out
	bool bHasRating;
	float fRating[MINACALC_NUM_SKILLSETS];
	int iRank;			// overall, 0 if unranked
	OnlineAccount(): bLoggedIn(false), bBusy(false), bHasRating(false), iRank(0) { }
};

struct OnlineLeaderboardEntry
{
	int iRank;
	CString sUsername;
	float fWife, fWifeJ4, fJudgeScale, fSSROverall;
	CString sStatus;
	CString sDate;
};

struct OnlineLeaderboard
{
	bool bLoading;
	CString sError;
	int iTotal;
	vector<OnlineLeaderboardEntry> vEntries;
	bool bHasMe;
	OnlineLeaderboardEntry me;
	OnlineLeaderboard(): bLoading(false), iTotal(0), bHasMe(false) { }
};

/* A line of the global chat. */
struct OnlineChatLine
{
	int iID;
	enum Kind { MESSAGE, ACTION, JOIN, PART } kind;	// ACTION is "/me ..."
	CString sUser;
	CString sText;
	time_t tAt;		// local clock
};

class OnlineManager
{
public:
	OnlineManager();
	~OnlineManager();

	void Update( float fDeltaTime );

	/* A server is configured. */
	bool IsEnabled() const;
	/* The game was built with an HTTPS client. */
	bool IsBuiltIn() const;
	/* The server as it should be shown, its name, and why it can't be used
	 * (a bad URL, the wrong API version), if it can't. */
	CString GetServerURL() const { return m_sServer; }
	CString GetServerName() const { return m_sServerName; }
	CString GetServerProblem() const;
	CString GetClockWarning() const { return m_sClockWarning; }
	CString GetRegisterURL() const { return m_sServer + "/register"; }
	int GetNumQueued() const { return m_Queue.size(); }

	/* Accounts, one per player side: the profile's, or the side's for a
	 * player without one. */
	const OnlineAccount &GetAccount( PlayerNumber pn );
	void Login( PlayerNumber pn, const CString &sUsername, const CString &sPassword );
	void Logout( PlayerNumber pn );
	/* Fetch the player's rating again if it's old. */
	void RefreshAccount( PlayerNumber pn );

	/* The online leaderboard of a chart at a rate, fetched when asked for and
	 * kept for a minute. */
	const OnlineLeaderboard &GetLeaderboard( PlayerNumber pn, const CString &sChartKey, float fRate );

	/* Gameplay.  BeginSong comes after WifeManager::BeginSong. */
	void BeginSong( PlayerNumber pn, const NoteDataWithScoring &nd, const Song *pSong, const Steps *pSteps );
	void EndSong( PlayerNumber pn, const NoteDataWithScoring &nd );
	bool IsRecording( PlayerNumber pn ) const { return m_Play[pn].bRecording; }
	void LogStep( PlayerNumber pn, bool bMineCross, int iCol, float fMusicSeconds, float fSearchSeconds, float fBPS,
		int iRow, float fOffset, TapNoteScore tns );
	void LogRelease( PlayerNumber pn, int iCol, float fMusicSeconds );
	void LogMiss( PlayerNumber pn, int iCol, int iRow, float fFrameSeconds );
	void LogHold( PlayerNumber pn, int iCol, int iRow, bool bOK, float fFrameSeconds );
	bool IsHeldInLog( PlayerNumber pn, int iCol ) const { return m_Play[pn].log.IsHeld( iCol ); }
	/* The Tab and ` keys changed the game's speed this frame. */
	void DebugTimeKeysUsed();
	/* ScreenGameplay started the song's music. */
	void MusicStarted();

	/* Results.  After WifeManager::RecordStage, for a play that is saved. */
	void RecordStage( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs );
	const OnlineStageResult &GetStageResult( PlayerNumber pn ) const { return m_Result[pn]; }
	/* Send a play that wasn't sent because OnlineAutoSubmit is off. */
	void SubmitStage( PlayerNumber pn );

	/* The global chat (F8), as the first logged-in player.  It's polled
	 * slowly while it's closed, which is also what shows the player as
	 * online, and quickly while it's open.  GetChatSerial changes whenever
	 * the lines, the online list or the status do. */
	bool CanChat();
	CString GetChatUser() const { return m_sChatUser; }
	const vector<OnlineChatLine> &GetChatLines() const { return m_ChatLines; }
	const vector<CString> &GetChatOnline() const { return m_ChatOnline; }
	CString GetChatStatus() const { return m_sChatStatus; }
	int GetChatSerial() const { return m_iChatSerial; }
	void SetChatOpen( bool bOpen );
	/* False if the last line is still on its way: try again in a moment. */
	bool ChatSay( const CString &sText );

	/* The validity rules (section 10.1) that can be checked before the song
	 * starts (see OnlineRules).  GetPlaySetup is what they look at for the
	 * player playing pSteps now; GetUnrankedRules adds every rule the play
	 * breaks to out, in order, and GetUnrankedReason says why the play can't
	 * be ranked, or "" if it can.  The online panel on the music select screen
	 * checks GetPlaySetup too, with the chart's notes as loaded. */
	static OnlinePlaySetup GetPlaySetup( PlayerNumber pn, const Steps *pSteps );
	static void GetUnrankedRules( PlayerNumber pn, const NoteData &nd, const Steps *pSteps, vector<OnlineRules::Rule> &out );
	static CString GetUnrankedReason( PlayerNumber pn, const NoteData &nd, const Steps *pSteps );

private:
	/* The server, as the preference had it when we last looked. */
	CString m_sServer;
	bool m_bServerOK;		// the URL is usable
	enum { INFO_NONE, INFO_ASKING, INFO_OK, INFO_FAILED, INFO_WRONG_API } m_InfoState;
	int m_iInfoRequest;
	float m_fInfoRetry;
	CString m_sServerName, m_sInfoError, m_sClockWarning;
	void CheckServer();

	struct Account
	{
		CString sFile;		// where the token is kept
		CString sToken;
		OnlineAccount info;
		int iLoginRequest, iLogoutRequest, iMeRequest;
		float fMeFetched;	// game time, or -1
		Account(): iLoginRequest(0), iLogoutRequest(0), iMeRequest(0), fMeFetched(-1) { }
	};
	Account m_Account[NUM_PLAYERS];
	CString GetAccountFile( PlayerNumber pn ) const;
	void LoadAccount( PlayerNumber pn );
	void ForgetToken( PlayerNumber pn, const CString &sWhy );
	CString ReadToken( const CString &sFile ) const;
	void UpdateAccounts();

	struct Board
	{
		OnlineLeaderboard lb;
		int iRequest;
		float fFetched;
	};
	std::map<CString, Board> m_Boards;
	void UpdateBoards();

	struct PlayData
	{
		bool bRecording;
		OnlineInputLog log;
		CString sUnranked;		// the first rule the play broke
		CString sPlayerOptions, sSongOptions;
		float fRate;			// exact, as played
		float fJudgeScale;
		CString sTimingJson;	// at the start, to catch offset and BPM changes
		CString sGameNotes;		// the game's judgments at the end, for test vectors
		CString sGameHolds;
		CString sGameMines;
		const Song *pSong;
		const Steps *pSteps;
		CString sChartKey;
		/* The play ticket. */
		CString sAccountFile;	// whose ticket it is
		int iTicketRequest;
		CString sTicket;
		CString sTicketError;
		time_t tSubmitFrom;		// local clock: not before the song's length has passed
		time_t tTicketExpires;
		/* The submission, kept until the ticket is there. */
		CString sBody;
		bool bWantSubmit;
		PlayData(): bRecording(false), fRate(1), fJudgeScale(1), pSong(NULL), pSteps(NULL), iTicketRequest(0),
			tSubmitFrom(0), tTicketExpires(0), bWantSubmit(false) { }
	};
	PlayData m_Play[NUM_PLAYERS];
	OnlineStageResult m_Result[NUM_PLAYERS];
	void UpdateTickets();
	void FinishStage( PlayerNumber pn );
	void RequestTicket( PlayerNumber pn );

	/* Scores on their way to the server, kept on disk until it has them. */
	struct Queued
	{
		CString sScoreKey;
		CString sServer;
		CString sAccountFile;
		CString sBody;		// read from the queue file when it's needed
		time_t tNotBefore, tExpires;
		int iTries;
		float fNextTry;		// game time
		int iRequest;
		PlayerNumber pn;	// whose results screen shows it, or PLAYER_INVALID
		Queued(): tNotBefore(0), tExpires(0), iTries(0), fNextTry(0), iRequest(0), pn(PLAYER_INVALID) { }
	};
	vector<Queued> m_Queue;
	bool m_bQueueLoaded;
	std::set<CString> m_Submitted;	// score keys the server has
	void LoadQueue();
	void Enqueue( PlayerNumber pn, const CString &sBody );
	void SaveQueued( const Queued &q ) const;
	void RemoveQueued( unsigned i );
	void UpdateQueue();
	void Submitted( const CString &sScoreKey );
	void SetResult( PlayerNumber pn, const CString &sScoreKey, OnlineStageResult::State state, const CString &sReason );
	void FetchRank( PlayerNumber pn );
	int m_iRankRequest[NUM_PLAYERS];

	/* The chat, for the account of m_ChatPlayer. */
	PlayerNumber m_ChatPlayer;
	CString m_sChatToken;		// when it changes, the chat starts over
	CString m_sChatUser;
	vector<OnlineChatLine> m_ChatLines;
	vector<CString> m_ChatOnline;
	CString m_sChatStatus;
	int m_iChatSerial;
	int m_iChatLastID;			// -1 before the first answer
	int m_iChatPollRequest, m_iChatSayRequest;
	float m_fChatNextPoll;		// game time
	bool m_bChatOpen;
	void UpdateChat();
	void ReadChat( const OnlineHttpResponse &r, PlayerNumber pn );

	bool m_bMusicPlaying;
	float m_fClockSince;
	void LogMusicClock( float fDeltaTime );

	static JsonValue ClientInfo();
	static JsonValue RateValue( float fRate );

	void Unranked( PlayerNumber pn, const CString &sWhy );
	CString MakeSubmission( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs, const CString &sTicket ) const;
	void WriteTestVector( PlayerNumber pn, const Song *pSong, const Steps *pSteps, const HighScore &hs );
};

extern OnlineManager *ONLINE;	// global and accessable from anywhere in our program

#endif
