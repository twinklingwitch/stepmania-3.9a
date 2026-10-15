/* The WinHTTP backend of OnlineHttp, for Windows.  WinHTTP is part of
 * Windows, so online play needs no extra DLLs; it checks certificates against
 * the system's store and uses the system's proxy settings. */
#include "global.h"
#include "OnlineHttp.h"
#include "RageUtil.h"

#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string>

bool OnlineHttp::IsBuiltIn() { return true; }

static const size_t MAX_RESPONSE = 8*1024*1024;

static std::wstring Widen( const CString &s )
{
	if( s.empty() )
		return std::wstring();
	const int n = MultiByteToWideChar( CP_UTF8, 0, s.data(), (int) s.size(), NULL, 0 );
	std::wstring w( n, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n );
	return w;
}

static CString Error( const char *szWhat )
{
	const DWORD iErr = GetLastError();
	switch( iErr )
	{
	case ERROR_WINHTTP_TIMEOUT:				return "timed out";
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:	return "couldn't find the server";
	case ERROR_WINHTTP_CANNOT_CONNECT:		return "couldn't connect to the server";
	case ERROR_WINHTTP_CONNECTION_ERROR:	return "the connection was broken";
	case ERROR_WINHTTP_SECURE_FAILURE:		return "the server's certificate isn't valid";
	}
	return ssprintf( "%s failed (WinHTTP error %lu)", szWhat, (unsigned long) iErr );
}

void OnlineHttpBackendInit()
{
}

void *OnlineHttpBackendCreate()
{
	/* Automatic proxy is Windows 8.1 and newer, which we require anyway. */
	HINTERNET h = WinHttpOpen( L"StepMania", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if( h == NULL )
		h = WinHttpOpen( L"StepMania", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	return h;
}

void OnlineHttpBackendDestroy( void *pContext )
{
	if( pContext )
		WinHttpCloseHandle( (HINTERNET) pContext );
}

void OnlineHttpBackendPerform( void *pContext, const OnlineHttpRequest &req, const CString &sUserAgent,
	const volatile bool *pbAbort, OnlineHttpResponse &out )
{
	out = OnlineHttpResponse();
	HINTERNET hSession = (HINTERNET) pContext;
	if( hSession == NULL )
	{
		out.sError = Error( "WinHttpOpen" );
		return;
	}

	const std::wstring sURL = Widen( req.sURL );
	URL_COMPONENTS uc;
	memset( &uc, 0, sizeof(uc) );
	uc.dwStructSize = sizeof(uc);
	uc.dwHostNameLength = (DWORD) -1;
	uc.dwUrlPathLength = (DWORD) -1;
	uc.dwExtraInfoLength = (DWORD) -1;
	uc.dwSchemeLength = (DWORD) -1;
	if( !WinHttpCrackUrl(sURL.c_str(), (DWORD) sURL.size(), 0, &uc) )
	{
		out.sError = "isn't a URL the game can use";
		return;
	}
	const std::wstring sHost( uc.lpszHostName, uc.dwHostNameLength );
	std::wstring sPath( uc.lpszUrlPath, uc.dwUrlPathLength );
	sPath += std::wstring( uc.lpszExtraInfo, uc.dwExtraInfoLength );
	if( sPath.empty() )
		sPath = L"/";
	const bool bHttps = uc.nScheme == INTERNET_SCHEME_HTTPS;

	HINTERNET hConnect = WinHttpConnect( hSession, sHost.c_str(), uc.nPort, 0 );
	if( hConnect == NULL )
	{
		out.sError = Error( "WinHttpConnect" );
		return;
	}
	HINTERNET hReq = WinHttpOpenRequest( hConnect, Widen(req.sMethod).c_str(), sPath.c_str(), NULL,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, bHttps? WINHTTP_FLAG_SECURE: 0 );
	if( hReq == NULL )
	{
		out.sError = Error( "WinHttpOpenRequest" );
		WinHttpCloseHandle( hConnect );
		return;
	}

	const int iTimeout = req.iTimeoutSeconds * 1000;
	WinHttpSetTimeouts( hReq, 10000, 10000, iTimeout, iTimeout );
	DWORD dw = WINHTTP_DECOMPRESSION_FLAG_ALL;
	WinHttpSetOption( hReq, WINHTTP_OPTION_DECOMPRESSION, &dw, sizeof(dw) );
	dw = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	WinHttpSetOption( hReq, WINHTTP_OPTION_REDIRECT_POLICY, &dw, sizeof(dw) );

	CString sHeaders = "Accept: application/json\r\nUser-Agent: " + sUserAgent + "\r\n";
	if( !req.sToken.empty() )
		sHeaders += "Authorization: Bearer " + req.sToken + "\r\n";
	if( !req.sBuild.empty() )
		sHeaders += "X-Build: " + req.sBuild + "\r\nX-Build-Signature: " + req.sSignature + "\r\n";
	CString sBody = req.sBody;
	if( req.sMethod == "POST" )
	{
		sHeaders += "Content-Type: application/json\r\n";
		CString sGzipped;
		if( req.bCompress && OnlineHttp::Gzip(req.sBody, sGzipped) )
		{
			sBody = sGzipped;
			sHeaders += "Content-Encoding: gzip\r\n";
		}
	}
	const std::wstring wsHeaders = Widen( sHeaders );

	bool bOK = WinHttpSendRequest( hReq, wsHeaders.c_str(), (DWORD) wsHeaders.size(),
		sBody.empty()? WINHTTP_NO_REQUEST_DATA: (LPVOID) sBody.data(), (DWORD) sBody.size(), (DWORD) sBody.size(), 0 ) != FALSE;
	if( bOK )
		bOK = WinHttpReceiveResponse( hReq, NULL ) != FALSE;
	if( !bOK )
	{
		out.sError = Error( "sending the request" );
		WinHttpCloseHandle( hReq );
		WinHttpCloseHandle( hConnect );
		return;
	}

	DWORD iStatus = 0, iSize = sizeof(iStatus);
	WinHttpQueryHeaders( hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		&iStatus, &iSize, WINHTTP_NO_HEADER_INDEX );
	wchar_t szRetry[32];
	iSize = sizeof(szRetry);
	if( WinHttpQueryHeaders(hReq, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, szRetry, &iSize, WINHTTP_NO_HEADER_INDEX) )
		out.iRetryAfter = _wtoi( szRetry );

	CString sResponse;
	for(;;)
	{
		if( *pbAbort )
		{
			out.sError = "cancelled";
			break;
		}
		DWORD iAvail = 0;
		if( !WinHttpQueryDataAvailable(hReq, &iAvail) )
		{
			out.sError = Error( "reading the answer" );
			break;
		}
		if( iAvail == 0 )
			break;
		if( sResponse.size() + iAvail > MAX_RESPONSE )
		{
			out.sError = "the server's answer was too big";
			break;
		}
		const size_t iOld = sResponse.size();
		sResponse.resize( iOld + iAvail );
		DWORD iRead = 0;
		if( !WinHttpReadData(hReq, &sResponse[iOld], iAvail, &iRead) )
		{
			out.sError = Error( "reading the answer" );
			break;
		}
		sResponse.resize( iOld + iRead );
	}

	WinHttpCloseHandle( hReq );
	WinHttpCloseHandle( hConnect );
	if( !out.sError.empty() )
		return;
	out.iStatus = (int) iStatus;
	out.sBody = sResponse;
}
