/* WifeManager - Keeps the Wife3 score of the song being played and the local
 * leaderboards.  Everything here is switched off by the Wife preference: with
 * it off no player is ever active, nothing is read or written and the rest of
 * the game never calls in here beyond a quick check.
 *
 * Player calls in while a song is played, ScreenEvaluation once to record the
 * score, and WifeOverlay reads what it shows. */

#ifndef WIFE_MANAGER_H
#define WIFE_MANAGER_H

#include "PlayerNumber.h"
#include "Wife.h"
#include "WifeReplay.h"
#include "WifeScores.h"

#include <map>
#include <set>

class NoteData;
class RageThread;
class Song;
class Steps;
struct HighScore;

/* How a song went, for the results screen to show. */
struct WifeStageResult
{
	bool bValid;
	float fWife;	// fraction
	float fRate;
	float fJudgeScale;
	CString sChartKey;
	CString sScoreKey;
	int iRankProfile, iCountProfile;	// among this profile's scores of the chart
	int iRankAll, iCountAll;			// among all the profiles'
	WifeStageResult(): bValid(false), fWife(0), fRate(1), fJudgeScale(1), iRankProfile(0), iCountProfile(0), iRankAll(0), iCountAll(0) { }
};

class WifeManager
{
public:
	WifeManager();
	~WifeManager();

	/* Starts reading the score files of the players' profiles on a thread, so
	 * the first leaderboard doesn't wait for a file of thousands of scores to
	 * be read.  Whatever needs the scores waits for the thread if it isn't
	 * done. */
	void Preload();

	/* The part Player uses while a song is played. */
	void BeginSong( PlayerNumber pn, const NoteData &nd, const Song *pSong, const Steps *pSteps );
	void EndSong( PlayerNumber pn ) { m_Player[pn].bActive = false; }
	/* Active from the start of a song to its end: the time Player reports to us. */
	bool IsActive( PlayerNumber pn ) const { return m_Player[pn].bActive; }
	/* There's a score for the last song played, until the next one starts. */
	bool HasScore( PlayerNumber pn ) const { return m_Player[pn].bScored; }
	void Press( PlayerNumber pn, int iColumn, float fSongSeconds, int iRow, float fOffset, int iType, int iSubType );
	void TapHit( PlayerNumber pn, int iColumn, int iRow, float fOffset );
	void TapMissed( PlayerNumber pn, int iColumn, int iRow, int iType, int iSubType );
	void MineHit( PlayerNumber pn, int iColumn, int iRow );
	void HoldDropped( PlayerNumber pn, int iColumn, int iRow, int iSubType );

	/* As a fraction.  The live one is over the notes judged so far, the final
	 * one over the whole chart. */
	float GetLivePercent( PlayerNumber pn ) const { return m_Player[pn].tally.GetLivePercent(); }
	float GetFinalPercent( PlayerNumber pn ) const { return m_Player[pn].tally.GetFinalPercent(); }
	const WifeTally &GetTally( PlayerNumber pn ) const { return m_Player[pn].tally; }
	/* The same play rescored at Judge 4: every hit at the play's own judge is
	 * worth what it would be at Judge 4.  SSRs and online scores use this. */
	const WifeTally &GetTallyJ4( PlayerNumber pn ) const { return m_Player[pn].tallyJ4; }
	const WifeReplay &GetReplay( PlayerNumber pn ) const { return m_Player[pn].replay; }
	/* The mean and standard deviation of the offsets of the notes that were
	 * hit, in seconds (positive is late).  False if none were hit. */
	bool GetOffsetStats( PlayerNumber pn, float &fMeanOut, float &fSDOut ) const;

	/* Called by ScreenEvaluation for a song that is going to be saved: adds the
	 * Wife score to hs (which goes into the profile's high scores), saves the
	 * replay and adds the score to the profile's and the machine's leaderboards. */
	void RecordStage( PlayerNumber pn, const Song *pSong, const Steps *pSteps, HighScore &hs );
	const WifeStageResult &GetStageResult( PlayerNumber pn ) const { return m_Result[pn]; }

	/* The chart key of a chart, worked out when asked for. */
	static CString GetChartKey( const Song *pSong, const Steps *pSteps );

	/* The best scores of a chart at a rate (at every rate if fRate is negative):
	 * of the player's profile (the machine profile if the player doesn't have
	 * one), or of all the profiles. */
	void GetLeaderboard( PlayerNumber pn, const CString &sChartKey, float fRate, bool bAllProfiles, vector<WifeScoreEntry> &out );

private:
	struct PlayerData
	{
		bool bActive;
		bool bScored;
		WifeTally tally;
		WifeTally tallyJ4;
		WifeReplay replay;
		CString sChartKey;
		float fRate;
		float fJudgeScale;
		std::set< std::pair<int,int> > holdsCounted;	// (column, head row) of holds already charged
		int iHits;
		double fOffsetSum, fOffsetSquares;	// of the hits' offsets
		PlayerData(): bActive(false), bScored(false), fRate(1), fJudgeScale(1), iHits(0), fOffsetSum(0), fOffsetSquares(0) { }
	};
	PlayerData m_Player[NUM_PLAYERS];
	WifeStageResult m_Result[NUM_PLAYERS];

	/* The scores of one profile directory, and where the ones of each chart are
	 * in them (a chart's leaderboard is looked up at every stop of the wheel). */
	struct ScoreSet
	{
		vector<WifeScoreEntry> v;
		std::map< CString, vector<unsigned> > byChart;
		void Add( const WifeScoreEntry &e );
	};

	/* What each profile directory's score file holds, kept up to date as scores
	 * are added. */
	std::map< CString, ScoreSet > m_Cache;
	const ScoreSet &GetScores( const CString &sProfileDir );
	void AddToCache( const CString &sProfileDir, const WifeScoreEntry &e, bool bAdded );

	RageThread *m_pPreload;
	vector<CString> m_PreloadDirs;
	vector<ScoreSet> m_PreloadScores;
	static int PreloadMain( void *p );
	void FinishPreload();
	CString GetProfileDir( PlayerNumber pn ) const;
	void GetAllProfileDirs( vector<CString> &out ) const;
};

extern WifeManager *WIFE;	// global and accessable from anywhere in our program

#endif
