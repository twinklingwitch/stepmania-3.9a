/* Checks the Discord Rich Presence protocol pieces: frames go out with the
 * right header and come back whole however they're split, the handshake and
 * SET_ACTIVITY commands are what Discord expects, and the activity keeps to
 * Discord's limits.  It also talks to a socket (a named pipe on Windows) like
 * Discord's. */
#include "global.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "DiscordIpc.h"
#include "OnlineJson.h"
#include "test_misc.h"

#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static int g_iFailures = 0;

static void Check( bool b, const CString &sWhat )
{
	if( b )
		return;
	LOG->Warn( "FAIL: %s", sWhat.c_str() );
	++g_iFailures;
}

static unsigned NumUnits16( const CString &s )
{
	/* What Discord counts: UTF-16 units. */
	unsigned n = 0;
	for( unsigned i = 0; i < s.size(); ++i )
	{
		const unsigned char c = (unsigned char) s[i];
		if( (c & 0xC0) == 0x80 )
			continue;
		n += (c & 0xF8) == 0xF0? 2: 1;
	}
	return n;
}

static void TestFrames()
{
	const CString sFrame = DiscordIpc::EncodeFrame( DiscordIpc::OP_FRAME, "{}" );
	Check( sFrame.size() == 10, "frame size" );
	Check( memcmp(sFrame.data(), "\x01\x00\x00\x00\x02\x00\x00\x00{}", 10) == 0, "frame header is little endian opcode and length" );

	const CString sBig = DiscordIpc::EncodeFrame( DiscordIpc::OP_PING, CString(300, 'x') );
	Check( (unsigned char) sBig[0] == 3 && (unsigned char) sBig[4] == 300 % 256 && (unsigned char) sBig[5] == 1, "length over 255" );

	/* Two frames, fed a byte at a time. */
	const CString sBoth = DiscordIpc::EncodeFrame( DiscordIpc::OP_PING, "{\"a\":1}" ) + DiscordIpc::EncodeFrame( DiscordIpc::OP_CLOSE, "{\"code\":4000}" );
	CString sBuf;
	vector<int> vOps;
	vector<CString> vJson;
	for( unsigned i = 0; i < sBoth.size(); ++i )
	{
		sBuf += sBoth[i];
		int iOp;
		CString sJson;
		DiscordIpc::DecodeResult r;
		while( (r = DiscordIpc::DecodeFrame(sBuf, iOp, sJson)) == DiscordIpc::DECODE_OK )
		{
			vOps.push_back( iOp );
			vJson.push_back( sJson );
		}
		Check( r == DiscordIpc::DECODE_NEED_MORE, "split frames never look bad" );
	}
	Check( vOps.size() == 2, "both frames decoded" );
	if( vOps.size() == 2 )
	{
		Check( vOps[0] == DiscordIpc::OP_PING && vJson[0] == "{\"a\":1}", "first frame" );
		Check( vOps[1] == DiscordIpc::OP_CLOSE && vJson[1] == "{\"code\":4000}", "second frame" );
	}
	Check( sBuf.empty(), "nothing left over" );

	/* An empty payload is fine. */
	sBuf = DiscordIpc::EncodeFrame( DiscordIpc::OP_PONG, "" );
	int iOp = -1;
	CString sJson = "x";
	Check( DiscordIpc::DecodeFrame(sBuf, iOp, sJson) == DiscordIpc::DECODE_OK && iOp == DiscordIpc::OP_PONG && sJson.empty(), "empty payload" );

	/* Garbage. */
	sBuf = CString( "\x07\x00\x00\x00\x00\x00\x00\x00", 8 );
	Check( DiscordIpc::DecodeFrame(sBuf, iOp, sJson) == DiscordIpc::DECODE_BAD, "unknown opcode" );
	sBuf = CString( "\x01\x00\x00\x00\x00\x00\x00\x10", 8 );
	Check( DiscordIpc::DecodeFrame(sBuf, iOp, sJson) == DiscordIpc::DECODE_BAD, "huge length" );
	sBuf = CString( "\x01\x00\x00", 3 );
	Check( DiscordIpc::DecodeFrame(sBuf, iOp, sJson) == DiscordIpc::DECODE_NEED_MORE, "short header" );
}

static void TestCommands()
{
	Check( DiscordIpc::Handshake("1559247505366188054") == "{\"v\":1,\"client_id\":\"1559247505366188054\"}",
		"handshake: " + DiscordIpc::Handshake("1559247505366188054") );

	const CString sSet = DiscordIpc::SetActivity( 4242, "{\"details\":\"Idle\"}", "4242-7" );
	Check( sSet == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4242,\"activity\":{\"details\":\"Idle\"}},\"nonce\":\"4242-7\"}", "set activity: " + sSet );
	const CString sClear = DiscordIpc::SetActivity( 4242, "", "4242-8" );
	Check( sClear == "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4242},\"nonce\":\"4242-8\"}", "clear activity: " + sClear );

	JsonValue v;
	CString sError;
	Check( JsonValue::Parse(sSet, v, sError) && v["args"]["activity"]["details"].GetString() == "Idle", "set activity parses" );

	Check( DiscordIpc::MillisecondsText(1791824860813LL) == "1791824860813", "milliseconds: " + DiscordIpc::MillisecondsText(1791824860813LL) );
	Check( DiscordIpc::NowMs() > 1700000000000LL, "the time now is in milliseconds" );
}

static void TestActivity()
{
	DiscordActivity a;
	a.sDetails = "Song 0-0 - Gen";
	a.sState = DiscordText::ChartLine( "Hard", 12, 1.2f, 24.314f, "Stream" );
	a.sLargeImage = "logo";
	a.sLargeText = "Stepmania 3.9";
	a.sSmallImage = "online";
	a.sSmallText = "tester on aurasm.online";
	a.iEndMs = 1791825137995LL;
	a.iPartySize = 2;
	a.iPartyMax = 2;
	a.AddButton( "AURA rankings", "https://aurasm.online" );
	a.AddButton( "tester's profile", "https://aurasm.online/users/tester" );
	const CString sWant =
		"{\"details\":\"Song 0-0 - Gen\",\"state\":\"Hard 12 \xC2\xB7 1.20x \xC2\xB7 MSD 24.31 Stream\","
		"\"timestamps\":{\"end\":1791825137995},"
		"\"assets\":{\"large_image\":\"logo\",\"large_text\":\"Stepmania 3.9\",\"small_image\":\"online\",\"small_text\":\"tester on aurasm.online\"},"
		"\"party\":{\"size\":[2,2]},"
		"\"buttons\":[{\"label\":\"AURA rankings\",\"url\":\"https://aurasm.online\"},{\"label\":\"tester's profile\",\"url\":\"https://aurasm.online/users/tester\"}]}";
	Check( a.ToJson() == sWant, "full activity: " + a.ToJson() );

	/* Short text is left out, long text clipped, bad buttons dropped. */
	DiscordActivity b;
	b.sDetails = "A";
	b.sState = CString( 200, 'x' );
	b.AddButton( "Plain http", "http://example.org" );
	b.AddButton( CString(40, 'L'), "https://example.org/1" );
	b.AddButton( "Two", "https://example.org/2" );
	b.AddButton( "Three", "https://example.org/3" );
	JsonValue v;
	CString sError;
	Check( JsonValue::Parse(b.ToJson(), v, sError), "clipped activity parses" );
	Check( !v.Has("details"), "one-character details left out" );
	Check( NumUnits16(v["state"].GetString()) == 128, "state clipped to 128" );
	Check( v["state"].GetString().Right(3) == "\xE2\x80\xA6", "clipped text ends in an ellipsis" );
	Check( !v.Has("timestamps") && !v.Has("assets") && !v.Has("party"), "nothing that wasn't set" );
	Check( v["buttons"].size() == 2, "at most two buttons, https only" );
	Check( NumUnits16(v["buttons"][0u]["label"].GetString()) == 32, "button label clipped to 32" );
	Check( v["buttons"][1u]["url"].GetString() == "https://example.org/2", "second button" );

	DiscordActivity c;
	c.sDetails = "In the menus";
	c.iStartMs = 1000;
	c.sLargeImage = "logo";
	c.sLargeText = "Stepmania 3.9";
	Check( c.ToJson() == "{\"details\":\"In the menus\",\"timestamps\":{\"start\":1000},\"assets\":{\"large_image\":\"logo\",\"large_text\":\"Stepmania 3.9\"}}", "menus: " + c.ToJson() );
}

static void TestText()
{
	/* Characters outside the BMP count twice, and are never cut in half. */
	CString sEmoji;
	for( int i = 0; i < 100; ++i )
		sEmoji += "\xF0\x9F\x8E\xB5";	// U+1F3B5
	const CString sClipped = DiscordIpc::Clip( sEmoji, 128 );
	Check( NumUnits16(sClipped) <= 128, "emoji clipped to 128 units" );
	Check( sClipped == sEmoji.substr(0, 63*4) + "\xE2\x80\xA6", "emoji clipped at a whole character" );
	Check( DiscordIpc::Clip("short", 128) == "short", "short text kept" );
	Check( DiscordIpc::Clip(CString(128, 'a'), 128) == CString(128, 'a'), "exactly the limit is kept" );
	Check( DiscordIpc::Clip("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 3) == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "three CJK characters fit in 3" );

	Check( DiscordText::ChartLine("Hard", 12, 1.0f, 0, NULL) == "Hard 12", "chart at 1.0x without MSD" );
	Check( DiscordText::ChartLine("Challenge", 14, 0.5f, 0, NULL) == "Challenge 14 \xC2\xB7 0.50x", "chart at a rate" );
	Check( DiscordText::ChartLine("Medium", 7, 1.0f, 15.5f, NULL) == "Medium 7 \xC2\xB7 MSD 15.50", "chart with MSD" );
	Check( DiscordText::Rate(1.2f) == "1.20x", "rate" );

	float fMSD[MINACALC_NUM_SKILLSETS] = { 24.31f, 24.31f, 19.0f, 15.2f, 21.0f, 12.0f, 10.0f, 18.0f };
	Check( DiscordText::DominantSkillset(fMSD) == 1, "stream stands out" );
	float fClose[MINACALC_NUM_SKILLSETS] = { 24.31f, 24.31f, 23.9f, 15.2f, 21.0f, 12.0f, 10.0f, 18.0f };
	Check( DiscordText::DominantSkillset(fClose) == -1, "stream and jumpstream are too close" );
	/* From real charts: Escapes (Challenge) is chordjack; Falling Falls is
	 * mostly Technical, which isn't a pattern, so it gets nothing. */
	float fCJ[MINACALC_NUM_SKILLSETS] = { 23.2f, 16.18f, 21.66f, 20.02f, 21.13f, 17.22f, 23.2f, 21.74f };
	Check( DiscordText::DominantSkillset(fCJ) == 6, "chordjack stands out" );
	float fTech[MINACALC_NUM_SKILLSETS] = { 21.77f, 19.22f, 20.01f, 17.94f, 20.02f, 14.5f, 15.37f, 21.77f };
	Check( DiscordText::DominantSkillset(fTech) == -1, "technical doesn't count" );
	float fLow[MINACALC_NUM_SKILLSETS] = { 20.59f, 14.18f, 16.9f, 15.86f, 20.38f, 10.9f, 13.91f, 20.29f };
	Check( DiscordText::DominantSkillset(fLow) == -1, "a pattern well under the Overall doesn't count" );
	float fZero[MINACALC_NUM_SKILLSETS] = { 0 };
	Check( DiscordText::DominantSkillset(fZero) == -1, "nothing for nothing" );

	Check( DiscordText::UrlSegment("tester_01") == "tester_01", "plain username" );
	Check( DiscordText::UrlSegment("a b/c?") == "a%20b%2Fc%3F", "escaped username" );
}

#if defined(WINDOWS)
static void TestSocket()
{
	/* A named pipe like Discord's, served from here. */
	const CString sName = ssprintf( "\\\\.\\pipe\\discord-ipc-test-%u", (unsigned) GetCurrentProcessId() );
	HANDLE hServer = CreateNamedPipeA( sName.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1, 4096, 4096, 0, NULL );
	Check( hServer != INVALID_HANDLE_VALUE, "test pipe" );

	DiscordConnection conn;
	Check( !conn.Open("\\\\.\\pipe\\no-such-discord-ipc"), "no Discord there" );
	Check( conn.Open(sName) && conn.IsOpen(), "connects" );
	Check( ConnectNamedPipe(hServer, NULL) || GetLastError() == ERROR_PIPE_CONNECTED, "the server sees the client" );

	Check( conn.Write(DiscordIpc::EncodeFrame(DiscordIpc::OP_HANDSHAKE, DiscordIpc::Handshake("1"))), "writes" );
	char buf[256];
	DWORD iGot = 0;
	ReadFile( hServer, buf, sizeof(buf), &iGot, NULL );
	CString sGot( buf, iGot );
	int iOp;
	CString sJson;
	Check( DiscordIpc::DecodeFrame(sGot, iOp, sJson) == DiscordIpc::DECODE_OK && iOp == DiscordIpc::OP_HANDSHAKE && sJson == "{\"v\":1,\"client_id\":\"1\"}", "the other end gets the handshake" );

	CString sBuf;
	Check( conn.Read(sBuf, 0) && sBuf.empty(), "nothing to read doesn't wait or fail" );
	const CString sPing = DiscordIpc::EncodeFrame( DiscordIpc::OP_PING, "{}" );
	DWORD iWritten = 0;
	Check( WriteFile(hServer, sPing.data(), sPing.size(), &iWritten, NULL) && iWritten == sPing.size(), "server writes" );
	Check( conn.Read(sBuf, 1000) && sBuf == sPing, "reads what's sent" );

	DisconnectNamedPipe( hServer );
	CloseHandle( hServer );
	CString sRest;
	Check( !conn.Read(sRest, 1000) && !conn.IsOpen(), "notices the other end closing" );
}
#else
static void TestSocket()
{
	const char *szPath = "discord-ipc-test";
	unlink( szPath );
	const int iListen = socket( AF_UNIX, SOCK_STREAM, 0 );
	struct sockaddr_un addr;
	memset( &addr, 0, sizeof(addr) );
	addr.sun_family = AF_UNIX;
	strcpy( addr.sun_path, szPath );
	Check( bind(iListen, (struct sockaddr *) &addr, sizeof(addr)) == 0 && listen(iListen, 1) == 0, "test socket" );

	DiscordConnection conn;
	Check( !conn.Open("no-such-discord-ipc"), "no Discord there" );
	Check( conn.Open(szPath) && conn.IsOpen(), "connects" );
	const int iServer = accept( iListen, NULL, NULL );

	Check( conn.Write(DiscordIpc::EncodeFrame(DiscordIpc::OP_HANDSHAKE, DiscordIpc::Handshake("1"))), "writes" );
	char buf[256];
	const ssize_t iGot = read( iServer, buf, sizeof(buf) );
	CString sGot( buf, iGot > 0? iGot: 0 );
	int iOp;
	CString sJson;
	Check( DiscordIpc::DecodeFrame(sGot, iOp, sJson) == DiscordIpc::DECODE_OK && iOp == DiscordIpc::OP_HANDSHAKE && sJson == "{\"v\":1,\"client_id\":\"1\"}", "the other end gets the handshake" );

	CString sBuf;
	Check( conn.Read(sBuf, 0) && sBuf.empty(), "nothing to read doesn't wait or fail" );
	const CString sPing = DiscordIpc::EncodeFrame( DiscordIpc::OP_PING, "{}" );
	Check( write(iServer, sPing.data(), sPing.size()) == (ssize_t) sPing.size(), "server writes" );
	Check( conn.Read(sBuf, 1000) && sBuf == sPing, "reads what's sent" );

	close( iServer );
	CString sRest;
	Check( !conn.Read(sRest, 1000) && !conn.IsOpen(), "notices the other end closing" );
	close( iListen );
	unlink( szPath );
}
#endif

int main( int argc, char *argv[] )
{
	test_handle_args( argc, argv );
	test_init();

	TestFrames();
	TestCommands();
	TestActivity();
	TestText();
	TestSocket();

	if( g_iFailures )
	{
		LOG->Warn( "%i failures", g_iFailures );
		exit( 1 );
	}
	LOG->Info( "test_discord: all passed" );
	exit( 0 );
}
