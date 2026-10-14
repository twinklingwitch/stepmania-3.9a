/* ChatOverlay - The global chat of the online server, IRC style, drawn over
 * everything by the system layer.  F8 opens and closes it anywhere but in the
 * editor (which has F8 for itself), as long as an online server is set.
 *
 * While it's open it takes the keyboard: typing goes into the line at the
 * bottom, Enter sends it ("/me waves" is an action), Esc or F8 closes it.
 * Key releases and other devices still reach the screen, so a held key isn't
 * left stuck.  The chat is the first logged-in player's (F7 on the music
 * select screen); the list on the right is who is online. */

#ifndef CHAT_OVERLAY_H
#define CHAT_OVERLAY_H

#include "ActorFrame.h"
#include "BitmapText.h"
#include "Quad.h"
#include "InputFilter.h"

class ChatOverlay : public ActorFrame
{
public:
	ChatOverlay();

	void Update( float fDeltaTime );
	void DrawPrimitives();

	/* Returns true if the key was ours. */
	bool Input( const DeviceInput &DeviceI, InputEventType type );

private:
	void Init();
	void SetOpen( bool bOpen );
	void Rebuild();
	void UpdateEntry();

	bool m_bInit;
	bool m_bOpen;
	CString m_sEntry;
	float m_fCursorBlink;
	int m_iSerial;		// OnlineManager's chat serial when the lines were last built
	bool m_bCouldChat;

	enum { NUM_LINES = 24 };
	Quad m_Back, m_Divider;
	BitmapText m_textTitle, m_textHint, m_textStatus;
	BitmapText m_textLine[NUM_LINES];
	int m_iLinesShown;
	BitmapText m_textOnline;
	BitmapText m_textEntry;
};

#endif
