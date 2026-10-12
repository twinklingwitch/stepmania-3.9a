/* OnlineHttp - HTTPS requests for the online server, off the main thread.
 *
 * Send() queues a request and returns at once; worker threads do the network
 * part and Poll() picks up the answer when it's there.  The game loop never
 * waits on the network.  Every request has a timeout.
 *
 * The network part is a backend: libcurl on Unix, WinHTTP on Windows, or none
 * when the game is built without online support, in which case every request
 * fails at once with an explanation.
 *
 * Certificates are always checked.  Plain http:// is only allowed to this
 * machine, or anywhere with OnlineAllowInsecureHttp=1 for testing.  Nothing
 * here logs request bodies or headers, which hold passwords and tokens. */

#ifndef ONLINE_HTTP_H
#define ONLINE_HTTP_H

struct OnlineHttpRequest
{
	CString sMethod;	// "GET" or "POST"
	CString sURL;
	CString sBody;		// JSON, or empty for none
	CString sToken;		// bearer token, or empty
	bool bCompress;		// send the body gzip compressed
	int iTimeoutSeconds;
	OnlineHttpRequest(): sMethod("GET"), bCompress(false), iTimeoutSeconds(30) { }
};

struct OnlineHttpResponse
{
	int iStatus;		// the HTTP status, or 0 if there was no answer
	CString sError;		// why there was no answer
	CString sBody;
	int iRetryAfter;	// seconds, from Retry-After, or -1
	OnlineHttpResponse(): iStatus(0), iRetryAfter(-1) { }
};

namespace OnlineHttp
{
	/* Built with a backend at all. */
	bool IsBuiltIn();

	/* "" if requests may go to sURL, or why not. */
	CString CheckURL( const CString &sURL, bool bAllowInsecure );

	/* Returns a handle for Poll. */
	int Send( const OnlineHttpRequest &req );
	/* True once the request is finished; the handle is then forgotten. */
	bool Poll( int iHandle, OnlineHttpResponse &out );
	/* Don't want the answer any more. */
	void Forget( int iHandle );

	/* Stops the workers.  Requests still going are abandoned. */
	void Shutdown();

	/* gzip, for request bodies. */
	bool Gzip( const CString &sIn, CString &sOut );
}

/* What a backend implements, in OnlineHttp_*.cpp.  These run on a worker
 * thread, each with its own context. */
void OnlineHttpBackendInit();
void *OnlineHttpBackendCreate();
void OnlineHttpBackendDestroy( void *pContext );
void OnlineHttpBackendPerform( void *pContext, const OnlineHttpRequest &req, const CString &sUserAgent,
	const volatile bool *pbAbort, OnlineHttpResponse &out );

#endif
