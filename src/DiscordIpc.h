/* DiscordIpc - Just enough of Discord's local IPC protocol for Rich Presence,
 * with no Discord SDK.
 *
 * The Discord client listens on a local socket (a Unix socket called
 * discord-ipc-0 to discord-ipc-9 in the user's runtime or temp directory, or the
 * named pipe \\.\pipe\discord-ipc-N on Windows).  Everything sent either way is
 * a frame: an opcode and a length, both 32-bit little endian, then that many
 * bytes of JSON.  We send a HANDSHAKE with the application ID, wait for the
 * READY it answers with, then send SET_ACTIVITY commands, answer PINGs and
 * ignore the rest.
 *
 * Nothing here goes over the network; the connection is to the Discord client
 * on this machine.  The frame and JSON parts have no game dependencies, so the
 * tests can check them. */

#ifndef DISCORD_IPC_H
#define DISCORD_IPC_H

#include "minacalc_c.h"

namespace DiscordIpc
{
	enum Opcode { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2, OP_PING = 3, OP_PONG = 4 };

	/* Discord's own frames are small; anything bigger is garbage. */
	const unsigned MAX_FRAME_SIZE = 64 * 1024;

	CString EncodeFrame( int iOpcode, const CString &sJson );

	enum DecodeResult { DECODE_NEED_MORE, DECODE_OK, DECODE_BAD };
	/* Takes the first whole frame off the front of sBuf. */
	DecodeResult DecodeFrame( CString &sBuf, int &iOpcode, CString &sJson );

	CString Handshake( const CString &sClientId );
	/* The SET_ACTIVITY command.  An empty sActivityJson clears the activity. */
	CString SetActivity( unsigned iPid, const CString &sActivityJson, const CString &sNonce );

	/* Where the Discord client may be listening, in the order to try them. */
	void GetEndpoints( vector<CString> &out );

	/* At most iMax characters (UTF-16 units, as Discord counts them), cut at a
	 * whole character, with "..." at the end if anything was cut. */
	CString Clip( const CString &s, unsigned iMax );

	/* Milliseconds since 1970 as JSON text. */
	CString MillisecondsText( int64_t iMs );
	/* The time now, in milliseconds since 1970 (Discord's timestamps). */
	int64_t NowMs();
	unsigned GetPid();
}

/* What Discord shows.  Text fields shorter than two characters are left out,
 * as Discord refuses them; longer ones are clipped to Discord's limits. */
struct DiscordActivity
{
	CString sDetails, sState;
	CString sLargeImage, sLargeText;
	CString sSmallImage, sSmallText;
	int64_t iStartMs, iEndMs;			// 0 for none
	int iPartySize, iPartyMax;			// 0 for none
	struct Button { CString sLabel, sURL; };
	vector<Button> vButtons;			// up to two

	DiscordActivity(): iStartMs(0), iEndMs(0), iPartySize(0), iPartyMax(0) { }
	void AddButton( const CString &sLabel, const CString &sURL );
	CString ToJson() const;
};

namespace DiscordText
{
	/* "Hard 12 · 1.20x · MSD 24.31 Stream": the rate only if it isn't 1.0, the
	 * MSD only if fMSD > 0, and the skillset only if szSkillset isn't NULL. */
	CString ChartLine( const CString &sDifficulty, int iMeter, float fRate, float fMSD, const char *szSkillset );
	/* "1.20x" */
	CString Rate( float fRate );
	/* The pattern skillset (Stream, Jumpstream, Handstream, JackSpeed or
	 * Chordjack) that clearly stands above the others and is close to the
	 * Overall, or -1. */
	int DominantSkillset( const float fMSD[MINACALC_NUM_SKILLSETS] );
	/* A URL path segment, percent-encoded. */
	CString UrlSegment( const CString &s );
}

/* A connection to one endpoint.  Reads never wait longer than asked; writes
 * give up after a second. */
class DiscordConnection
{
public:
	DiscordConnection();
	~DiscordConnection() { Close(); }

	bool Open( const CString &sEndpoint );
	void Close();
	bool IsOpen() const;

	/* False if the connection is gone. */
	bool Write( const CString &sData );
	/* Appends whatever arrives within iTimeoutMs.  False if the connection is gone. */
	bool Read( CString &sBuf, int iTimeoutMs );

private:
#if defined(WINDOWS)
	void *m_hPipe;
#else
	int m_iFD;
#endif
};

#endif
