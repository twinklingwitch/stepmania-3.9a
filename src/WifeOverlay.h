/* WifeOverlay - What the Wife option and online scores show, drawn over
 * everything by the system layer.  It doesn't depend on the theme (it uses a
 * font every theme falls back to) and draws nothing and handles no keys unless
 * the Wife preference is on or an online server is set.
 *
 *   music select:  F5 shows or hides the local leaderboard of the chart that is
 *                  selected, with its difficulty (MSD) per skillset at the current
 *                  rate in a column beside it; F6
 *                  switches it between the scores of the player's profile and
 *                  those of all profiles.  F7 shows the online panel: the
 *                  chart's online leaderboard and the player's rating (the
 *                  same column), or why the chart can't be ranked, or a
 *                  login form (F7 again goes to the other player, then hides;
 *                  F6 logs out).
 *   gameplay:      a small Wife% counter for each player in a corner of the
 *                  screen (the ShowWifeInGameplay preference turns it off).
 *   results:       F5 shows or hides this play's Wife%, where it ranks, its
 *                  SSRs and what happened to it online.  F7 sends it if
 *                  OnlineAutoSubmit is off. */

#ifndef WIFE_OVERLAY_H
#define WIFE_OVERLAY_H

#include "ActorFrame.h"
#include "BitmapText.h"
#include "Quad.h"
#include "PlayerNumber.h"
#include "WifeScores.h"
#include "InputFilter.h"
#include "ChartMSD.h"
#include "OnlineRules.h"

#include <map>
#include <set>

class Steps;

/* The eight skillsets one under the other: names on the left, values lined up
 * on the right, Overall first and bigger.  It shows a message instead if
 * there are no values. */
class SkillTable
{
public:
	void Load( const CString &sFont );
	/* pValues is NULL if there is nothing to show but sNote. */
	void Set( const CString &sHead, const CString &sSub, const float *pValues, const CString &sNote, bool bNoteIsWarning = false );
	/* Puts it in the column from fLeft to fRight, starting at fTop.  Returns
	 * where it ends. */
	float Layout( float fLeft, float fRight, float fTop );
	void Draw();

private:
	BitmapText m_textHead, m_textSub, m_textNote;
	BitmapText m_textName[MINACALC_NUM_SKILLSETS], m_textValue[MINACALC_NUM_SKILLSETS];
	CString m_sNote;
	bool m_bHasValues;
	bool m_bHasSub;
};

/* The character a key types, with a US layout as ScreenTextEntry has it,
 * or '\0' for a key that types nothing. */
char TypedChar( const DeviceInput &DeviceI );

class WifeOverlay : public ActorFrame
{
public:
	WifeOverlay();

	void Update( float fDeltaTime );
	void DrawPrimitives();

	/* Returns true if the key was ours. */
	bool Input( const DeviceInput &DeviceI, InputEventType type );

private:
	void Init();
	void SetPanel( const CString &sTitle, const CString &sSubtitle, const CString &sHint,
		const CString &sRanks, const CString &sWifes, const CString &sNames, const CString &sInfo,
		const CString &sMessage = "" );
	void SetBodyPanel( const CString &sTitle, const CString &sSubtitle, const CString &sHint, const CString &sBody );
	void LayoutTables( float fTop );
	CString ClipName( const CString &s );
	void UpdateSelectPanel( float fDeltaTime );
	void UpdateOnlinePanel( float fDeltaTime );
	void UpdateEvalPanel();
	void UpdateCounters();
	void SetMSDTable( const Steps *pSteps, float fRate, bool bPending );
	bool FormInput( const DeviceInput &DeviceI, InputEventType type );

	bool m_bInit;

	bool m_bShowSelect;		// the music select leaderboard
	bool m_bAllProfiles;	// ... for all profiles, not just the player's
	bool m_bShowEval;		// the results panel
	bool m_bShowOnline;		// the online panel on the music select screen
	PlayerNumber m_OnlinePlayer;	// whose it is

	/* The login form, while it's open. */
	bool m_bFormOpen;
	bool m_bFormOnPassword;
	CString m_sFormUser, m_sFormPassword;
	/* Keys pressed while the form was open and not let go yet. */
	set<int> m_FormKeys;

	/* What the select panel was last built for, so it's only redone on a change. */
	const Steps *m_pSelectSteps;
	float m_fSelectRate;
	bool m_bSelectAll;
	/* The MSD is worked out once the selection has stayed put for a moment. */
	float m_fSettled;
	bool m_bMSDPending;

	/* What the online panel found out about the selected chart, until another
	 * is selected: the rules its notes break, and whether the calculator rates
	 * it, which is asked once the selection has stayed put. */
	const Steps *m_pOnlineSteps;
	CString m_sOnlineKey;
	vector<OnlineRules::Rule> m_vOnlineNoteRules;
	float m_fOnlineSettled;
	bool m_bOnlineCalcChecked;
	bool m_bOnlineNotRated;

	bool m_bPanelVisible;
	Quad m_PanelBack;
	BitmapText m_textTitle, m_textSubtitle, m_textHint;
	BitmapText m_textRanks, m_textWifes, m_textNames, m_textInfo;
	BitmapText m_textMessage, m_textBody, m_textMeasure;
	map<CString,CString> m_ClipCache;
	/* The skillsets go in a column on the right (two of them, side by side,
	 * on the results screen); m_iTables is how many are shown. */
	SkillTable m_Table[2];
	int m_iTables;
	float m_fTablesBottom;
	Quad m_Divider;
	bool m_bPanelIsBody;
	float m_fRowY;
	float m_fPanelBottom;


	Quad m_CounterBack[NUM_PLAYERS];
	BitmapText m_textCounter[NUM_PLAYERS];
	bool m_bCounterVisible[NUM_PLAYERS];
};

#endif
