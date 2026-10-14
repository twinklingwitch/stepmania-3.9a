/* OnlineRoom - Multiplayer rooms on the online server (section 8.6 of the
 * server's docs/API.md), osu! style: players meet in a room, the host picks a
 * chart, everyone who has it and is ready plays it at once, and the room
 * keeps a live scoreboard and the results of the last round.
 *
 * This is the client's side: it keeps the room we're in up to date (a poll
 * about once a second, or the answer to whatever we last did), finds the
 * room's chart in our songs, and drives the music select and gameplay
 * screens through a round.  RoomOverlay (F10) is how the player sees it and
 * types commands.  It uses the same account as the chat: the first player
 * who is logged in. */

#ifndef ONLINE_ROOM_H
#define ONLINE_ROOM_H

#include "OnlineManager.h"
#include "OnlineJson.h"

class Song;
class Steps;

enum RoomJudgment
{
	RJ_MARVELOUS, RJ_PERFECT, RJ_GREAT, RJ_GOOD, RJ_BOO, RJ_MISS, RJ_HIT_MINE, RJ_HELD, RJ_LET_GO,
	NUM_ROOM_JUDGMENTS
};

struct OnlineRoomChart
{
	bool bSet;
	CString sChartKey;
	CString sSongDir;		// "Group/Folder"
	CString sDifficulty;
	int iMeter;
	CString sTitle, sSubtitle, sArtist;
	float fRate;
	OnlineRoomChart(): bSet(false), iMeter(0), fRate(1) { }
	/* "Title Subtitle (Hard 9) 1.10x" */
	CString Describe() const;
};

struct OnlineRoomScore
{
	bool bSet;
	float fWife;
	int iCombo, iMaxCombo;
	int iJudgments[NUM_ROOM_JUDGMENTS];
	bool bFailed;
	OnlineRoomScore(): bSet(false), fWife(0), iCombo(0), iMaxCombo(0), bFailed(false)
	{
		for( int i = 0; i < NUM_ROOM_JUDGMENTS; ++i )
			iJudgments[i] = 0;
	}
};

struct OnlineRoomMember
{
	CString sUser;
	bool bHost, bReady, bHasChart, bPlaying, bLoaded, bDone;
	OnlineRoomScore score;
	OnlineRoomMember(): bHost(false), bReady(false), bHasChart(false), bPlaying(false), bLoaded(false), bDone(false) { }
};

struct OnlineRoomResult
{
	int iRank;
	CString sUser;
	bool bFinished;		// played to the end, didn't leave
	OnlineRoomScore score;
};

struct OnlineRoomListing
{
	int iID;
	CString sName, sHost, sState;
	bool bLocked;
	int iPlayers, iMaxPlayers;
	OnlineRoomChart chart;
};

class OnlineRoom
{
public:
	OnlineRoom();

	void Update( float fDeltaTime );

	/* Logged in to a server that can be used. */
	bool CanUse();

	/* The rooms on the server, fetched when asked for. */
	void RefreshList();
	const vector<OnlineRoomListing> &GetList() const { return m_List; }
	bool IsListLoading() const { return m_iListRequest != 0; }
	CString GetListError() const { return m_sListError; }

	/* Things to do.  They go to the server one at a time, in order; what
	 * goes wrong ends up as a notice in GetLines. */
	void Create( const CString &sName, const CString &sPassword );
	void Join( int iID, const CString &sPassword );
	void Leave();
	void Say( const CString &sText );
	void SetReady( bool bReady );
	/* The host: the chart and rate the music select screen has now. */
	void PickCurrent();
	void Start( bool bForce );
	void Kick( const CString &sUser );
	void GiveHost( const CString &sUser );
	void Rename( const CString &sName );
	void SetPassword( const CString &sPassword );
	/* A line only we see, for command help and errors. */
	void Notice( const CString &sText );

	/* The room we're in. */
	bool InRoom() const { return m_iRoomID != 0; }
	bool IsJoining() const { return m_bJoining; }
	int GetID() const { return m_iRoomID; }
	CString GetName() const { return m_sName; }
	CString GetState() const { return m_sState; }	// lobby, loading, playing
	int GetRound() const { return m_iRound; }
	bool IsLocked() const { return m_bLocked; }
	CString GetUser() const { return m_sUser; }
	bool IsHost() const;
	const OnlineRoomMember *GetMe() const;
	const OnlineRoomChart &GetChart() const { return m_Chart; }
	const vector<OnlineRoomMember> &GetMembers() const { return m_Members; }
	const vector<OnlineChatLine> &GetLines() const { return m_Lines; }
	bool HasLastRound() const { return m_iLastRound != 0; }
	int GetLastRound() const { return m_iLastRound; }
	const OnlineRoomChart &GetLastRoundChart() const { return m_LastChart; }
	const vector<OnlineRoomResult> &GetLastResults() const { return m_LastResults; }
	/* Changes whenever anything above does. */
	int GetSerial() const { return m_iSerial; }

	/* The room's chart in our songs, or why it isn't there. */
	bool HaveChart() const { return m_pSong != NULL && m_pSteps != NULL; }
	Song *GetSong() const { return m_pSong; }
	Steps *GetSteps() const { return m_pSteps; }
	CString GetChartProblem() const { return m_sChartProblem; }

	/* Gameplay: we're playing the round that's on (loading or playing). */
	bool IsInRound() const;
	/* Our Wife% in the round so far, for the scoreboard. */
	float GetLiveWife() const { return m_fLiveWife; }
	/* ScreenGameplay has loaded and waits for everyone else... */
	void Loaded();
	/* ... until this says the song may start. */
	bool MayStart() const;

private:
	CString m_sServer, m_sToken, m_sUser;
	PlayerNumber m_SessionPlayer;
	void Reset( const CString &sWhy );

	/* The room. */
	int m_iRoomID;
	bool m_bJoining;
	CString m_sName, m_sState;
	int m_iRound;
	bool m_bLocked;
	int m_iVersion;
	OnlineRoomChart m_Chart;
	vector<OnlineRoomMember> m_Members;
	vector<OnlineChatLine> m_Lines;
	int m_iLastLine;		// the server's last line id, -1 before the first answer
	int m_iLocalLine;		// ids for notices, below zero
	int m_iLastRound;
	OnlineRoomChart m_LastChart;
	vector<OnlineRoomResult> m_LastResults;
	int m_iSerial;

	/* Requests: one poll, one action at a time, the list. */
	struct Action
	{
		CString sMethod, sPath;
		JsonValue body;		// "after" is added when it's sent
	};
	vector<Action> m_Actions;
	int m_iPollRequest, m_iActionRequest, m_iListRequest;
	float m_fNextPoll;
	void Queue( const CString &sMethod, const CString &sPath, const JsonValue &body );
	void SendNextAction();
	void ReadRoom( const OnlineHttpResponse &r, bool bAction );
	void ParseRoom( const JsonValue &v );

	vector<OnlineRoomListing> m_List;
	CString m_sListError;

	/* The chart. */
	CString m_sLocatedKey;	// the chart key m_pSong/m_pSteps were looked up for
	Song *m_pSong;
	Steps *m_pSteps;
	CString m_sChartProblem;
	CString m_sReportedKey;	// the chart we last told the room we have or not
	CString m_sSelectedKey;	// the chart we last put the wheel on
	void LocateChart();

	/* The round, as we play it. */
	int m_iStartedRound;		// the round we told ScreenSelectMusic to start
	int m_iLoadedRound;
	int m_iGameplayRound;		// the round ScreenGameplay was up for
	int m_iDoneRound;			// the round we sent our last progress for
	float m_fWaitingSince;
	float m_fLiveWife, m_fFinalWife;	// ours, as of gameplay's last frame
	void UpdateRound();
	JsonValue ProgressBody( bool bDone, bool bQuit ) const;
};

extern OnlineRoom *ROOM;	// global and accessable from anywhere in our program

#endif
