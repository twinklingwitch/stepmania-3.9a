/* ChatOverlay - The global chat of the online server, IRC style, drawn over
 * everything by the system layer.  F8 opens and closes it anywhere but in the
 * editor and gameplay (which have F8 for themselves), as long as an online
 * server is set.
 *
 * While it's open it takes the keyboard: typing goes into the line at the
 * bottom, Enter sends it ("/me waves" is an action), Esc or F8 closes it.
 * Other devices still reach the screen, and so does the release of a key
 * that was down before it opened, so a held key isn't left stuck.  The chat is the first logged-in player's (F7 on the music
 * select screen); the list on the right is who is online.
 *
 * ChatPanel is what it shares with the rooms' panel (RoomOverlay): the box,
 * the lines, the column on the right and the line being typed. */

#ifndef CHAT_OVERLAY_H
#define CHAT_OVERLAY_H

#include "ActorFrame.h"
#include "BitmapText.h"
#include "Quad.h"
#include "InputFilter.h"

#include <set>

struct OnlineChatLine;

class ChatPanel : public ActorFrame
{
public:
	ChatPanel( RageKeySym OpenKey );

	bool IsOpen() const { return m_bOpen; }
	virtual void SetOpen( bool bOpen );

	/* Returns true if the key was ours.  Open, the panel takes the keyboard;
	 * another panel's key closes it and is passed on.  A key it took is
	 * also its when it's let go, open or not. */
	virtual bool Input( const DeviceInput &DeviceI, InputEventType type );

	void DrawPrimitives();

protected:
	struct Line
	{
		CString sText;
		RageColor color;
		Line( const CString &s, RageColor c ): sText(s), color(c) { }
	};
	static Line FormatLine( const OnlineChatLine &l, const CString &sMe );

	/* The panel's text.  Lines go oldest first; as many of the newest as fit
	 * are shown. */
	void SetTexts( const CString &sTitle, const CString &sSub, const CString &sHint, const CString &sStatus, const CString &sSide );
	void SetLines( const vector<Line> &lines );
	void SetEntryEnabled( bool bEnabled );

	/* What Enter sends; true if it was taken and the line can be cleared. */
	virtual bool Submit( const CString &sText ) = 0;
	/* Draws what's shown with the panel closed. */
	virtual void DrawClosed() { }

	void UpdatePanel( float fDeltaTime );
	static bool CanOpenHere();

	bool m_bInit;

private:
	void Init();
	void UpdateEntry();
	void Layout();

	RageKeySym m_OpenKey;
	bool m_bOpen;
	std::set<int> m_KeysDown;	// keys we took that haven't been let go
	CString m_sEntry;
	bool m_bEntryEnabled;
	float m_fCursorBlink;

	enum { NUM_LINES = 24 };
	Quad m_Back, m_Divider;
	BitmapText m_textTitle, m_textSub, m_textHint, m_textStatus;
	BitmapText m_textLine[NUM_LINES];
	int m_iLinesShown;
	vector<Line> m_Lines;
	BitmapText m_textSide;
	BitmapText m_textEntry;
};

class ChatOverlay : public ChatPanel
{
public:
	ChatOverlay();
	void SetOpen( bool bOpen );
	void Update( float fDeltaTime );

protected:
	bool Submit( const CString &sText );

private:
	void Rebuild();
	int m_iSerial;		// OnlineManager's chat serial when the lines were last built
	bool m_bCouldChat;
};

#endif
