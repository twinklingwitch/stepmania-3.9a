/* The libcurl backend of OnlineHttp, for Unix. */
#include "global.h"
#include "OnlineHttp.h"
#include "RageUtil.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <strings.h>

bool OnlineHttp::IsBuiltIn() { return true; }

/* Nothing the server sends us is this big. */
static const size_t MAX_RESPONSE = 8*1024*1024;

void OnlineHttpBackendInit()
{
	/* Not thread safe in old versions, so before the workers start. */
	curl_global_init( CURL_GLOBAL_DEFAULT );
}

/* A handle per worker, so connections to the server are kept open between requests. */
void *OnlineHttpBackendCreate()
{
	return curl_easy_init();
}

void OnlineHttpBackendDestroy( void *pContext )
{
	if( pContext )
		curl_easy_cleanup( (CURL *) pContext );
}

namespace
{
	struct Transfer
	{
		CString sBody;
		int iRetryAfter;
		bool bTooBig;
		const volatile bool *pbAbort;
	};
}

static size_t WriteBody( char *p, size_t size, size_t n, void *pUser )
{
	Transfer *t = (Transfer *) pUser;
	if( t->sBody.size() + size*n > MAX_RESPONSE )
	{
		t->bTooBig = true;
		return 0;
	}
	t->sBody.append( p, size*n );
	return size*n;
}

static size_t WriteHeader( char *p, size_t size, size_t n, void *pUser )
{
	Transfer *t = (Transfer *) pUser;
	CString sLine( p, size*n );
	if( sLine.size() > 12 && !strncasecmp(sLine.c_str(), "Retry-After:", 12) )
		t->iRetryAfter = atoi( sLine.c_str() + 12 );
	return size*n;
}

static int Progress( void *pUser, curl_off_t, curl_off_t, curl_off_t, curl_off_t )
{
	Transfer *t = (Transfer *) pUser;
	return *t->pbAbort? 1: 0;
}

void OnlineHttpBackendPerform( void *pContext, const OnlineHttpRequest &req, const CString &sUserAgent,
	const volatile bool *pbAbort, OnlineHttpResponse &out )
{
	out = OnlineHttpResponse();
	CURL *c = (CURL *) pContext;
	if( c == NULL )
	{
		out.sError = "couldn't start libcurl";
		return;
	}
	curl_easy_reset( c );

	Transfer t;
	t.iRetryAfter = -1;
	t.bTooBig = false;
	t.pbAbort = pbAbort;

	char szError[CURL_ERROR_SIZE];
	szError[0] = '\0';

	curl_easy_setopt( c, CURLOPT_URL, req.sURL.c_str() );
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt( c, CURLOPT_PROTOCOLS_STR, "http,https" );
#else
	curl_easy_setopt( c, CURLOPT_PROTOCOLS, (long) (CURLPROTO_HTTP | CURLPROTO_HTTPS) );
#endif
	curl_easy_setopt( c, CURLOPT_NOSIGNAL, 1L );
	curl_easy_setopt( c, CURLOPT_FOLLOWLOCATION, 0L );
	curl_easy_setopt( c, CURLOPT_SSL_VERIFYPEER, 1L );
	curl_easy_setopt( c, CURLOPT_SSL_VERIFYHOST, 2L );
	curl_easy_setopt( c, CURLOPT_CONNECTTIMEOUT, 10L );
	curl_easy_setopt( c, CURLOPT_TIMEOUT, (long) req.iTimeoutSeconds );
	curl_easy_setopt( c, CURLOPT_USERAGENT, sUserAgent.c_str() );
	curl_easy_setopt( c, CURLOPT_ACCEPT_ENCODING, "" );	// everything libcurl can decode, gzip included
	curl_easy_setopt( c, CURLOPT_ERRORBUFFER, szError );
	curl_easy_setopt( c, CURLOPT_WRITEFUNCTION, WriteBody );
	curl_easy_setopt( c, CURLOPT_WRITEDATA, &t );
	curl_easy_setopt( c, CURLOPT_HEADERFUNCTION, WriteHeader );
	curl_easy_setopt( c, CURLOPT_HEADERDATA, &t );
	curl_easy_setopt( c, CURLOPT_XFERINFOFUNCTION, Progress );
	curl_easy_setopt( c, CURLOPT_XFERINFODATA, &t );
	curl_easy_setopt( c, CURLOPT_NOPROGRESS, 0L );

	struct curl_slist *pHeaders = NULL;
	pHeaders = curl_slist_append( pHeaders, "Accept: application/json" );
	CString sAuth;
	if( !req.sToken.empty() )
	{
		sAuth = "Authorization: Bearer " + req.sToken;
		pHeaders = curl_slist_append( pHeaders, sAuth.c_str() );
	}
	CString sBuild, sSignature;
	if( !req.sBuild.empty() )
	{
		sBuild = "X-Build: " + req.sBuild;
		sSignature = "X-Build-Signature: " + req.sSignature;
		pHeaders = curl_slist_append( pHeaders, sBuild.c_str() );
		pHeaders = curl_slist_append( pHeaders, sSignature.c_str() );
	}

	CString sBody = req.sBody;
	if( req.sMethod == "POST" )
	{
		pHeaders = curl_slist_append( pHeaders, "Content-Type: application/json" );
		CString sGzipped;
		if( req.bCompress && OnlineHttp::Gzip(req.sBody, sGzipped) )
		{
			sBody = sGzipped;
			pHeaders = curl_slist_append( pHeaders, "Content-Encoding: gzip" );
		}
		curl_easy_setopt( c, CURLOPT_POST, 1L );
		curl_easy_setopt( c, CURLOPT_POSTFIELDS, sBody.data() );
		curl_easy_setopt( c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) sBody.size() );
	}
	curl_easy_setopt( c, CURLOPT_HTTPHEADER, pHeaders );

	const CURLcode res = curl_easy_perform( c );
	curl_slist_free_all( pHeaders );
	/* Don't keep the token around in the handle. */
	curl_easy_setopt( c, CURLOPT_HTTPHEADER, (struct curl_slist *) NULL );

	if( res != CURLE_OK )
	{
		if( t.bTooBig )
			out.sError = "the server's answer was too big";
		else if( *pbAbort )
			out.sError = "cancelled";
		else
			out.sError = szError[0]? CString(szError): CString(curl_easy_strerror(res));
		return;
	}

	long iStatus = 0;
	curl_easy_getinfo( c, CURLINFO_RESPONSE_CODE, &iStatus );
	out.iStatus = (int) iStatus;
	out.sBody = t.sBody;
	out.iRetryAfter = t.iRetryAfter;
}
