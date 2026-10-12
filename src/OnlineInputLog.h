/* OnlineInputLog - Everything a player did during a song, raw, so the online
 * server can judge the play itself (section 7 of the server's docs/API.md).
 *
 * Unlike the Wife replay, which keeps what each press hit, this keeps every
 * press and release of every column with the music time it happened at, the
 * mines a held key crossed, and the misses and hold results the game decided
 * on a frame.  It's only recorded for online play and test vectors. */

#ifndef ONLINE_INPUT_LOG_H
#define ONLINE_INPUT_LOG_H

#include "GameConstantsAndTypes.h"

struct OnlineLogSettings
{
	float fJudgeScale, fJudgeAdd;
	float fMarvelous, fPerfect, fGreat, fGood, fBoo, fOK, fMine, fAttack;
	float fPadStickSeconds;
	float fConstantUpdateDelta;
	bool bShowMarvelous;
	bool b5thScoring;
	int iStartCombo;
	bool bMirror;
	bool bAutoplay;
	bool bDebugTimeKeysUsed;
};

class OnlineInputLog
{
public:
	OnlineInputLog() { Clear(); }
	void Clear();

	/* sTimingJson is the song's timing as the chart export writes it. */
	void Begin( int iColumns, float fRate, float fGlobalOffset, const CString &sTimingJson, const OnlineLogSettings &settings );

	/* A key press, or a held key crossing a mine (bMineCross), as Step saw it. */
	void Press( bool bMineCross, int iCol, float fMusicSeconds, float fSearchSeconds, float fBPS,
		int iRow, float fOffset, TapNoteScore tns );
	void Release( int iCol, float fMusicSeconds );
	void Miss( int iCol, int iRow, float fFrameSeconds );
	void Hold( int iCol, int iRow, bool bOK, float fFrameSeconds );
	void End( float fFrameSeconds );

	/* Is the column down, going by the presses and releases so far? */
	bool IsHeld( int iCol ) const;
	unsigned GetNumEvents() const { return m_Events.size(); }
	OnlineLogSettings &GetSettings() { return m_Settings; }

	CString ToJson() const;

	/* "marvelous", "hit_mine", ... as the log and the server name them. */
	static const char *TapNoteScoreName( TapNoteScore tns );

private:
	struct Event
	{
		char cType;	// P C R X H E
		bool bOK;
		int iCol, iRow;
		float fTime, fSearch, fBPS, fOffset;
		TapNoteScore tns;
	};
	vector<Event> m_Events;
	vector<bool> m_bHeld;

	int m_iColumns;
	float m_fRate;
	float m_fGlobalOffset;
	CString m_sTiming;
	OnlineLogSettings m_Settings;
};

#endif
