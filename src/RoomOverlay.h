/* RoomOverlay - The multiplayer rooms (OnlineRoom), IRC style: F10 opens a
 * panel like the chat's, where commands are typed and anything else is said
 * in the room.
 *
 *   /list               the rooms on the server (shown while not in one)
 *   /create [name]      make a room and be its host
 *   /join <#> [pass]    join a room by its number
 *   /leave
 *   /ready, /unready    you have the chart and want to play it
 *   /pick               (host) the chart and rate selected on the wheel
 *   /start [force]      (host) start the round; force leaves out who isn't ready
 *   /kick <name>, /host <name>, /name <text>, /password [pass]   (host)
 *
 * With the panel closed, a line at the top of the screen says what the room
 * is doing; in gameplay it's the live scoreboard, and after a round, the
 * results. */

#ifndef ROOM_OVERLAY_H
#define ROOM_OVERLAY_H

#include "ChatOverlay.h"

class RoomOverlay : public ChatPanel
{
public:
	RoomOverlay();
	void SetOpen( bool bOpen );
	void Update( float fDeltaTime );

protected:
	bool Submit( const CString &sText );
	void DrawClosed();

private:
	void Rebuild();
	void RebuildBar();
	void Command( const CString &sCommand, const CString &sArgs );

	int m_iSerial;		// OnlineRoom's serial when the panel was last built
	float m_fListAge;	// seconds since the room list was asked for

	bool m_bBarInit;
	bool m_bBarVisible;
	Quad m_BarBack;
	BitmapText m_textBar;
	CString m_sBar;
};

#endif
