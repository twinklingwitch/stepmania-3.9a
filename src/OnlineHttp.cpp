#include "global.h"
#include "OnlineHttp.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"

#include <deque>
#include <map>
#include <zlib.h>

/* Two, so a big score upload doesn't hold up a leaderboard. */
static const int NUM_WORKERS = 2;

namespace
{
	struct Job
	{
		OnlineHttpRequest req;
		OnlineHttpResponse resp;
		bool bDone;
		bool bForgotten;
	};

	RageMutex g_Mutex( "OnlineHttp" );
	RageSemaphore *g_pWork = NULL;	// one count per queued job
	std::deque<Job *> g_Queue;
	std::map<int, Job *> g_Jobs;
	int g_iNextHandle = 1;
	int g_iBusy = 0;
	volatile bool g_bQuit = false;
	bool g_bStarted = false;
	RageThread g_Workers[NUM_WORKERS];
	CString g_sUserAgent;
}

static const char *Platform()
{
#if defined(WINDOWS)
	return "windows";
#elif defined(DARWIN)
	return "macos";
#elif defined(FREEBSD)
	return "freebsd";
#elif defined(OPENBSD)
	return "openbsd";
#else
	return "linux";
#endif
}

static int WorkerMain( void * )
{
	void *pContext = OnlineHttpBackendCreate();
	for(;;)
	{
		g_pWork->Wait( false );
		if( g_bQuit )
			break;

		Job *pJob;
		{
			LockMut( g_Mutex );
			if( g_Queue.empty() )
				continue;
			pJob = g_Queue.front();
			g_Queue.pop_front();
			++g_iBusy;
		}

		OnlineHttpResponse resp;
		OnlineHttpBackendPerform( pContext, pJob->req, g_sUserAgent, &g_bQuit, resp );

		LockMut( g_Mutex );
		--g_iBusy;
		if( pJob->bForgotten )
			delete pJob;
		else
		{
			pJob->resp = resp;
			pJob->bDone = true;
		}
	}
	OnlineHttpBackendDestroy( pContext );
	return 0;
}

static void Start()
{
	if( g_bStarted )
		return;
	g_bStarted = true;
	g_sUserAgent = ssprintf( "StepMania/3.9 (%s; online 1)", Platform() );
	OnlineHttpBackendInit();
	g_pWork = new RageSemaphore( "OnlineHttp" );
	for( int i = 0; i < NUM_WORKERS; ++i )
	{
		g_Workers[i].SetName( ssprintf("Online HTTP %i", i) );
		g_Workers[i].Create( WorkerMain, NULL );
	}
}

int OnlineHttp::Send( const OnlineHttpRequest &req )
{
	Start();

	Job *pJob = new Job;
	pJob->req = req;
	pJob->bDone = false;
	pJob->bForgotten = false;

	/* The path only: queries and bodies can hold things that aren't ours to log. */
	CString sPath = req.sURL;
	const size_t iQuery = sPath.find( '?' );
	if( iQuery != CString::npos )
		sPath.erase( iQuery );
	LOG->Trace( "Online: %s %s", req.sMethod.c_str(), sPath.c_str() );

	int iHandle;
	{
		LockMut( g_Mutex );
		iHandle = g_iNextHandle++;
		g_Jobs[iHandle] = pJob;
		g_Queue.push_back( pJob );
	}
	g_pWork->Post();
	return iHandle;
}

bool OnlineHttp::Poll( int iHandle, OnlineHttpResponse &out )
{
	LockMut( g_Mutex );
	std::map<int, Job *>::iterator it = g_Jobs.find( iHandle );
	if( it == g_Jobs.end() )
	{
		out = OnlineHttpResponse();
		out.sError = "unknown request";
		return true;
	}
	Job *pJob = it->second;
	if( !pJob->bDone )
		return false;
	out = pJob->resp;
	g_Jobs.erase( it );
	delete pJob;
	return true;
}

void OnlineHttp::Forget( int iHandle )
{
	LockMut( g_Mutex );
	std::map<int, Job *>::iterator it = g_Jobs.find( iHandle );
	if( it == g_Jobs.end() )
		return;
	Job *pJob = it->second;
	g_Jobs.erase( it );

	std::deque<Job *>::iterator q = std::find( g_Queue.begin(), g_Queue.end(), pJob );
	if( q != g_Queue.end() )
	{
		g_Queue.erase( q );
		delete pJob;
	}
	else if( pJob->bDone )
		delete pJob;
	else
		pJob->bForgotten = true;	// the worker deletes it
}

void OnlineHttp::Shutdown()
{
	if( !g_bStarted )
		return;
	g_bQuit = true;
	for( int i = 0; i < NUM_WORKERS; ++i )
		g_pWork->Post();

	/* Requests notice g_bQuit and give up, but a backend may only notice at its
	 * timeout; don't hold up quitting for that. */
	bool bBusy;
	{
		LockMut( g_Mutex );
		bBusy = g_iBusy > 0;
	}
	if( bBusy )
		return;
	for( int i = 0; i < NUM_WORKERS; ++i )
		g_Workers[i].Wait();
}

/* "https://host:port/path" -> host, in lower case. */
static CString GetHost( const CString &sURL, CString &sScheme )
{
	const size_t iColon = sURL.find( "://" );
	if( iColon == CString::npos )
		return "";
	sScheme = sURL.substr( 0, iColon );
	sScheme.MakeLower();
	CString sRest = sURL.substr( iColon + 3 );
	const size_t iSlash = sRest.find_first_of( "/?#" );
	if( iSlash != CString::npos )
		sRest.erase( iSlash );
	/* No user:password@ in server URLs. */
	if( sRest.find('@') != CString::npos )
		return "";
	CString sHost = sRest;
	if( sHost.size() && sHost[0] == '[' )
	{
		const size_t iEnd = sHost.find( ']' );
		if( iEnd == CString::npos )
			return "";
		sHost = sHost.substr( 0, iEnd + 1 );
	}
	else
	{
		const size_t iPort = sHost.find( ':' );
		if( iPort != CString::npos )
			sHost.erase( iPort );
	}
	sHost.MakeLower();
	return sHost;
}

CString OnlineHttp::CheckURL( const CString &sURL, bool bAllowInsecure )
{
	CString sScheme;
	const CString sHost = GetHost( sURL, sScheme );
	if( sHost.empty() )
		return "isn't a URL the game can use";
	if( sScheme == "https" )
		return "";
	if( sScheme != "http" )
		return "must start with https://";
	if( sHost == "localhost" || sHost == "127.0.0.1" || sHost == "[::1]" || bAllowInsecure )
		return "";
	return "must use https:// (plain http:// is only for this machine)";
}

bool OnlineHttp::Gzip( const CString &sIn, CString &sOut )
{
	z_stream zs;
	memset( &zs, 0, sizeof(zs) );
	/* 15 + 16: a gzip header rather than a zlib one. */
	if( deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK )
		return false;
	sOut.resize( deflateBound(&zs, sIn.size()) );
	zs.next_in = (Bytef *) sIn.data();
	zs.avail_in = sIn.size();
	zs.next_out = (Bytef *) &sOut[0];
	zs.avail_out = sOut.size();
	const int ret = deflate( &zs, Z_FINISH );
	deflateEnd( &zs );
	if( ret != Z_STREAM_END )
		return false;
	sOut.resize( zs.total_out );
	return true;
}
