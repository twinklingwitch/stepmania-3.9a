#include "global.h"
#include "DiscordIpc.h"
#include "OnlineJson.h"
#include "RageUtil.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(WINDOWS)
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static void PutLE32( CString &s, unsigned u )
{
	for( int i = 0; i < 4; ++i )
		s += (char) ((u >> (8*i)) & 0xFF);
}

static unsigned GetLE32( const CString &s, unsigned iPos )
{
	unsigned u = 0;
	for( int i = 0; i < 4; ++i )
		u |= ((unsigned) (unsigned char) s[iPos + i]) << (8*i);
	return u;
}

CString DiscordIpc::EncodeFrame( int iOpcode, const CString &sJson )
{
	CString s;
	PutLE32( s, (unsigned) iOpcode );
	PutLE32( s, sJson.size() );
	s += sJson;
	return s;
}

DiscordIpc::DecodeResult DiscordIpc::DecodeFrame( CString &sBuf, int &iOpcode, CString &sJson )
{
	if( sBuf.size() < 8 )
		return DECODE_NEED_MORE;
	const unsigned iOp = GetLE32( sBuf, 0 );
	const unsigned iLen = GetLE32( sBuf, 4 );
	if( iOp > OP_PONG || iLen > MAX_FRAME_SIZE )
		return DECODE_BAD;
	if( sBuf.size() < 8 + iLen )
		return DECODE_NEED_MORE;
	iOpcode = (int) iOp;
	sJson = sBuf.substr( 8, iLen );
	sBuf.erase( 0, 8 + iLen );
	return DECODE_OK;
}

CString DiscordIpc::Handshake( const CString &sClientId )
{
	JsonValue v = JsonValue::Object();
	v.Set( "v", JsonValue::Int(1) );
	v.Set( "client_id", JsonValue::String(sClientId) );
	return v.Write();
}

CString DiscordIpc::SetActivity( unsigned iPid, const CString &sActivityJson, const CString &sNonce )
{
	JsonValue args = JsonValue::Object();
	args.Set( "pid", JsonValue::Number(iPid, ssprintf("%u", iPid)) );
	if( !sActivityJson.empty() )
		args.Set( "activity", JsonValue::Raw(sActivityJson) );

	JsonValue v = JsonValue::Object();
	v.Set( "cmd", JsonValue::String("SET_ACTIVITY") );
	v.Set( "args", args );
	v.Set( "nonce", JsonValue::String(sNonce) );
	return v.Write();
}

CString DiscordIpc::MillisecondsText( int64_t iMs )
{
	if( iMs <= 0 )
		return "0";
	char buf[32];
	int i = sizeof(buf) - 1;
	buf[i] = 0;
	while( iMs > 0 && i > 0 )
	{
		buf[--i] = (char) ('0' + (int) (iMs % 10));
		iMs /= 10;
	}
	return CString( buf + i );
}

/* The length in bytes of the UTF-8 character starting at p, or 1 for a byte
 * that doesn't start a valid one (JsonQuote takes those as Latin-1). */
static int Utf8Length( const unsigned char *p, const unsigned char *pEnd )
{
	int iLen;
	if( p[0] < 0x80 )			return 1;
	else if( (p[0] & 0xE0) == 0xC0 )	iLen = 2;
	else if( (p[0] & 0xF0) == 0xE0 )	iLen = 3;
	else if( (p[0] & 0xF8) == 0xF0 )	iLen = 4;
	else					return 1;
	if( pEnd - p < iLen )
		return 1;
	for( int i = 1; i < iLen; ++i )
		if( (p[i] & 0xC0) != 0x80 )
			return 1;
	return iLen;
}

CString DiscordIpc::Clip( const CString &s, unsigned iMax )
{
	const unsigned char *pBegin = (const unsigned char *) s.data();
	const unsigned char *pEnd = pBegin + s.size();

	/* Where the text has to be cut to leave room for the ellipsis. */
	size_t iCut = CString::npos;
	unsigned iUnits = 0;
	for( const unsigned char *p = pBegin; p < pEnd; )
	{
		const int iLen = Utf8Length( p, pEnd );
		const unsigned iCharUnits = iLen == 4? 2: 1;
		if( iCut == CString::npos && iUnits + iCharUnits > iMax - 1 )
			iCut = p - pBegin;
		iUnits += iCharUnits;
		if( iUnits > iMax )
		{
			CString sOut = s.substr( 0, iCut );
			TrimRight( sOut );
			return sOut + "\xE2\x80\xA6";	// U+2026
		}
		p += iLen;
	}
	return s;
}

static unsigned NumUnits( const CString &s )
{
	const unsigned char *p = (const unsigned char *) s.data();
	const unsigned char *pEnd = p + s.size();
	unsigned iUnits = 0;
	while( p < pEnd )
	{
		const int iLen = Utf8Length( p, pEnd );
		iUnits += iLen == 4? 2: 1;
		p += iLen;
	}
	return iUnits;
}

/* Discord wants 2 to 128 characters in its text fields. */
static void SetText( JsonValue &obj, const char *szKey, const CString &s, unsigned iMax )
{
	CString sText = s;
	TrimLeft( sText );
	TrimRight( sText );
	if( NumUnits(sText) < 2 )
		return;
	obj.Set( szKey, JsonValue::String(DiscordIpc::Clip(sText, iMax)) );
}

void DiscordActivity::AddButton( const CString &sLabel, const CString &sURL )
{
	Button b;
	b.sLabel = sLabel;
	b.sURL = sURL;
	vButtons.push_back( b );
}

CString DiscordActivity::ToJson() const
{
	JsonValue v = JsonValue::Object();
	SetText( v, "details", sDetails, 128 );
	SetText( v, "state", sState, 128 );

	if( iStartMs > 0 || iEndMs > 0 )
	{
		JsonValue t = JsonValue::Object();
		if( iStartMs > 0 )
			t.Set( "start", JsonValue::Number((double) iStartMs, DiscordIpc::MillisecondsText(iStartMs)) );
		if( iEndMs > 0 )
			t.Set( "end", JsonValue::Number((double) iEndMs, DiscordIpc::MillisecondsText(iEndMs)) );
		v.Set( "timestamps", t );
	}

	JsonValue a = JsonValue::Object();
	if( !sLargeImage.empty() )
	{
		a.Set( "large_image", JsonValue::String(sLargeImage) );
		SetText( a, "large_text", sLargeText, 128 );
	}
	if( !sSmallImage.empty() )
	{
		a.Set( "small_image", JsonValue::String(sSmallImage) );
		SetText( a, "small_text", sSmallText, 128 );
	}
	if( a.size() )
		v.Set( "assets", a );

	if( iPartySize > 0 && iPartyMax >= iPartySize )
	{
		JsonValue size = JsonValue::Array();
		size.Append( JsonValue::Int(iPartySize) );
		size.Append( JsonValue::Int(iPartyMax) );
		JsonValue p = JsonValue::Object();
		p.Set( "size", size );
		v.Set( "party", p );
	}

	JsonValue buttons = JsonValue::Array();
	for( unsigned i = 0; i < vButtons.size() && buttons.size() < 2; ++i )
	{
		const Button &b = vButtons[i];
		/* Discord only takes web links, and drops the whole activity for a bad one. */
		if( b.sURL.Left(8) != "https://" || b.sURL.size() > 512 || NumUnits(b.sLabel) < 1 )
			continue;
		JsonValue o = JsonValue::Object();
		o.Set( "label", JsonValue::String(DiscordIpc::Clip(b.sLabel, 32)) );
		o.Set( "url", JsonValue::String(b.sURL) );
		buttons.Append( o );
	}
	if( buttons.size() )
		v.Set( "buttons", buttons );

	return v.Write();
}

static const char *g_szDot = " \xC2\xB7 ";	// U+00B7

CString DiscordText::Rate( float fRate )
{
	return ssprintf( "%.2fx", fRate );
}

CString DiscordText::ChartLine( const CString &sDifficulty, int iMeter, float fRate, float fMSD, const char *szSkillset )
{
	CString s = sDifficulty;
	if( iMeter > 0 )
		s += ssprintf( s.empty()? "%i": " %i", iMeter );
	if( fabsf(fRate - 1.0f) > 0.001f )
	{
		if( !s.empty() )
			s += g_szDot;
		s += Rate( fRate );
	}
	if( fMSD > 0 )
	{
		if( !s.empty() )
			s += g_szDot;
		s += ssprintf( "MSD %.2f", fMSD );
		if( szSkillset != NULL )
			s += ssprintf( " %s", szSkillset );
	}
	return s;
}

int DiscordText::DominantSkillset( const float fMSD[MINACALC_NUM_SKILLSETS] )
{
	/* The pattern skillsets: Stream, Jumpstream, Handstream, JackSpeed and
	 * Chordjack.  Stamina and Technical go with the others and say little
	 * about what a chart is. */
	static const int iPatterns[] = { 1, 2, 3, 5, 6 };
	int iBest = -1;
	float fBest = 0, fSecond = 0;
	for( unsigned i = 0; i < ARRAYSIZE(iPatterns); ++i )
	{
		const float f = fMSD[iPatterns[i]];
		if( iBest == -1 || f > fBest )
		{
			fSecond = iBest == -1? 0: fBest;
			fBest = f;
			iBest = iPatterns[i];
		}
		else if( f > fSecond )
			fSecond = f;
	}

	if( fBest <= 0 )
		return -1;
	/* Clearly ahead of the next one, and what the chart's difficulty is about. */
	if( fBest - fSecond < max(1.0f, fBest * 0.05f) )
		return -1;
	if( fBest < fMSD[0] * 0.93f )
		return -1;
	return iBest;
}

CString DiscordText::UrlSegment( const CString &s )
{
	CString sOut;
	for( unsigned i = 0; i < s.size(); ++i )
	{
		const unsigned char c = (unsigned char) s[i];
		if( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '.' || c == '_' || c == '~' )
			sOut += (char) c;
		else
			sOut += ssprintf( "%%%02X", c );
	}
	return sOut;
}

#if defined(WINDOWS)

int64_t DiscordIpc::NowMs()
{
	FILETIME ft;
	GetSystemTimeAsFileTime( &ft );
	const int64_t i100ns = ((int64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	/* From 1601 to 1970. */
	return i100ns / 10000 - (int64_t) 11644473600000LL;
}

unsigned DiscordIpc::GetPid()
{
	return ::GetCurrentProcessId();
}

void DiscordIpc::GetEndpoints( vector<CString> &out )
{
	out.clear();
	for( int i = 0; i < 10; ++i )
		out.push_back( ssprintf("\\\\.\\pipe\\discord-ipc-%i", i) );
}

DiscordConnection::DiscordConnection(): m_hPipe( INVALID_HANDLE_VALUE ) { }

bool DiscordConnection::IsOpen() const
{
	return m_hPipe != INVALID_HANDLE_VALUE;
}

bool DiscordConnection::Open( const CString &sEndpoint )
{
	Close();
	HANDLE h = CreateFileA( sEndpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL );
	if( h == INVALID_HANDLE_VALUE )
		return false;
	m_hPipe = h;
	return true;
}

void DiscordConnection::Close()
{
	if( m_hPipe == INVALID_HANDLE_VALUE )
		return;
	CloseHandle( (HANDLE) m_hPipe );
	m_hPipe = INVALID_HANDLE_VALUE;
}

bool DiscordConnection::Write( const CString &sData )
{
	if( !IsOpen() )
		return false;
	size_t iDone = 0;
	while( iDone < sData.size() )
	{
		DWORD iWritten = 0;
		if( !WriteFile((HANDLE) m_hPipe, sData.data() + iDone, (DWORD) (sData.size() - iDone), &iWritten, NULL) || iWritten == 0 )
		{
			Close();
			return false;
		}
		iDone += iWritten;
	}
	return true;
}

bool DiscordConnection::Read( CString &sBuf, int iTimeoutMs )
{
	if( !IsOpen() )
		return false;
	/* A pipe opened like this can't wait with a timeout, so look and nap. */
	const DWORD iStart = GetTickCount();
	for(;;)
	{
		DWORD iAvail = 0;
		if( !PeekNamedPipe((HANDLE) m_hPipe, NULL, 0, NULL, &iAvail, NULL) )
		{
			Close();
			return false;
		}
		if( iAvail > 0 )
		{
			char buf[4096];
			DWORD iRead = 0;
			if( !ReadFile((HANDLE) m_hPipe, buf, min((DWORD) sizeof(buf), iAvail), &iRead, NULL) )
			{
				Close();
				return false;
			}
			sBuf.append( buf, iRead );
			return true;
		}
		if( (int) (GetTickCount() - iStart) >= iTimeoutMs )
			return true;
		Sleep( 10 );
	}
}

#else

int64_t DiscordIpc::NowMs()
{
	struct timeval tv;
	gettimeofday( &tv, NULL );
	return (int64_t) tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

unsigned DiscordIpc::GetPid()
{
	return (unsigned) getpid();
}

void DiscordIpc::GetEndpoints( vector<CString> &out )
{
	vector<CString> vDirs;
	const char *szRuntime = getenv( "XDG_RUNTIME_DIR" );
	if( szRuntime != NULL && szRuntime[0] )
	{
		CString sRuntime = szRuntime;
		vDirs.push_back( sRuntime );
		vDirs.push_back( sRuntime + "/app/com.discordapp.Discord" );
		vDirs.push_back( sRuntime + "/snap.discord" );
	}
	const char *szVars[] = { "TMPDIR", "TMP", "TEMP" };
	for( unsigned i = 0; i < ARRAYSIZE(szVars); ++i )
	{
		const char *sz = getenv( szVars[i] );
		if( sz != NULL && sz[0] )
			vDirs.push_back( sz );
	}
	vDirs.push_back( "/tmp" );

	out.clear();
	for( unsigned d = 0; d < vDirs.size(); ++d )
	{
		CString sDir = vDirs[d];
		while( sDir.size() > 1 && sDir[sDir.size()-1] == '/' )
			sDir.erase( sDir.size()-1 );
		/* Each directory once, in the first place it comes up. */
		bool bSeen = false;
		for( unsigned e = 0; e < d; ++e )
		{
			CString sOther = vDirs[e];
			while( sOther.size() > 1 && sOther[sOther.size()-1] == '/' )
				sOther.erase( sOther.size()-1 );
			if( sOther == sDir )
				bSeen = true;
		}
		if( bSeen )
			continue;
		for( int i = 0; i < 10; ++i )
			out.push_back( ssprintf("%s/discord-ipc-%i", sDir.c_str(), i) );
	}
}

DiscordConnection::DiscordConnection(): m_iFD( -1 ) { }

bool DiscordConnection::IsOpen() const
{
	return m_iFD != -1;
}

bool DiscordConnection::Open( const CString &sEndpoint )
{
	Close();

	struct sockaddr_un addr;
	memset( &addr, 0, sizeof(addr) );
	if( sEndpoint.size() >= sizeof(addr.sun_path) )
		return false;
	addr.sun_family = AF_UNIX;
	memcpy( addr.sun_path, sEndpoint.c_str(), sEndpoint.size() );

	const int fd = socket( AF_UNIX, SOCK_STREAM, 0 );
	if( fd == -1 )
		return false;
	fcntl( fd, F_SETFD, FD_CLOEXEC );
#if defined(SO_NOSIGPIPE)
	const int iOne = 1;
	setsockopt( fd, SOL_SOCKET, SO_NOSIGPIPE, &iOne, sizeof(iOne) );
#endif
	/* Connecting to a local socket doesn't wait: it's there or it isn't. */
	if( connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1 )
	{
		close( fd );
		return false;
	}
	fcntl( fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK );
	m_iFD = fd;
	return true;
}

void DiscordConnection::Close()
{
	if( m_iFD == -1 )
		return;
	close( m_iFD );
	m_iFD = -1;
}

bool DiscordConnection::Write( const CString &sData )
{
	if( !IsOpen() )
		return false;
#if defined(MSG_NOSIGNAL)
	const int iFlags = MSG_NOSIGNAL;
#else
	const int iFlags = 0;
#endif
	size_t iDone = 0;
	while( iDone < sData.size() )
	{
		const ssize_t iSent = send( m_iFD, sData.data() + iDone, sData.size() - iDone, iFlags );
		if( iSent > 0 )
		{
			iDone += iSent;
			continue;
		}
		if( iSent == -1 && errno == EINTR )
			continue;
		if( iSent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) )
		{
			struct pollfd pfd;
			pfd.fd = m_iFD;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			if( poll(&pfd, 1, 1000) > 0 && !(pfd.revents & (POLLERR|POLLHUP)) )
				continue;
		}
		Close();
		return false;
	}
	return true;
}

bool DiscordConnection::Read( CString &sBuf, int iTimeoutMs )
{
	if( !IsOpen() )
		return false;
	struct pollfd pfd;
	pfd.fd = m_iFD;
	pfd.events = POLLIN;
	pfd.revents = 0;
	const int iReady = poll( &pfd, 1, iTimeoutMs );
	if( iReady == -1 )
		return errno == EINTR;
	if( iReady == 0 )
		return true;

	for(;;)
	{
		char buf[4096];
		const ssize_t iGot = recv( m_iFD, buf, sizeof(buf), 0 );
		if( iGot > 0 )
		{
			sBuf.append( buf, iGot );
			if( sBuf.size() > 4 * DiscordIpc::MAX_FRAME_SIZE )
				break;	// garbage; DecodeFrame will say so
			continue;
		}
		if( iGot == -1 && errno == EINTR )
			continue;
		if( iGot == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) )
			break;
		/* 0 is the other end closing. */
		Close();
		return false;
	}
	return true;
}

#endif
