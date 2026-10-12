/* WifeReplay - What a player did during a song, in the text format Etterna uses
 * for its input data (Replay::WriteInputData, input data version 3, before it
 * is compressed), so tools that read those can read these.  Only recorded when
 * the Wife preference is on.
 *
 * The file is one header line:
 *
 *   chartkey scorekey rate songoffset globaloffset mods rngseed version
 *
 * then one line for every key press:
 *
 *   column pressed songseconds nearestrow offset nearesttype nearestsubtype
 *
 * with the note nearest to the press (row -1 and offset 0 if there was none),
 * its offset in seconds (positive is late) and its Etterna note type and hold
 * subtype, and then the things a press doesn't show:
 *
 *   H row column [subtype]       a hold that was dropped (or whose head was missed)
 *   M row column                 a mine that was hit
 *   X column row type subtype    a note that was missed
 *
 * Ported from Etterna's Replay.cpp (MIT license, Copyright (c) 2016-2023 Etterna
 * <etternadev@gmail.com>; the license text is in Wife.h). */

#ifndef WIFE_REPLAY_H
#define WIFE_REPLAY_H

/* Etterna's numbers for the kinds of notes. */
enum
{
	WIFE_TYPE_EMPTY = 0,
	WIFE_TYPE_TAP = 1,
	WIFE_TYPE_HOLD_HEAD = 2,
	WIFE_TYPE_MINE = 4,
	WIFE_TYPE_INVALID = 9
};
enum
{
	WIFE_SUBTYPE_HOLD = 0,
	WIFE_SUBTYPE_ROLL = 1,
	WIFE_SUBTYPE_INVALID = 3
};

struct WifeInputEvent
{
	bool bPress;
	int iColumn;
	float fSongSeconds;
	int iNearestRow;
	float fOffset;
	int iNearestType;
	int iNearestSubType;
};

struct WifeHoldDrop
{
	int iRow;
	int iColumn;
	int iSubType;
};

struct WifeMineHit
{
	int iRow;
	int iColumn;
};

struct WifeMiss
{
	int iColumn;
	int iRow;
	int iType;
	int iSubType;
};

struct WifeReplay
{
	CString sChartKey;
	CString sScoreKey;
	float fRate;
	float fSongOffset;
	float fGlobalOffset;
	CString sMods;
	int iRngSeed;

	vector<WifeInputEvent> inputs;
	vector<WifeHoldDrop> holds;
	vector<WifeMineHit> mines;
	vector<WifeMiss> misses;

	WifeReplay() { Clear(); }
	void Clear();

	/* Everything above as text, and back. */
	CString ToString() const;
	bool FromString( const CString &s );

	bool Write( const CString &sPath ) const;
	bool Read( const CString &sPath );

	/* Wife3 points worked out from the recorded presses, misses, mines and
	 * drops alone, for a judge window scale.  A press counts as a hit if it was
	 * on a tap or hold head within the boo window, as in 3.9. */
	float GetPoints( float fTimingScale ) const;
	/* The notes that were judged: hit within the window, or missed. */
	int GetNumNotes( float fTimingScale ) const;
};

#endif
