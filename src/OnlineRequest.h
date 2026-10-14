/* OnlineRequest - The small helpers every part of the online client uses to
 * talk to the server (docs/API.md in the server's repository): sending a
 * request to an API path, reading the answer, and saying what went wrong. */

#ifndef ONLINE_REQUEST_H
#define ONLINE_REQUEST_H

#include <time.h>

class JsonValue;
struct OnlineHttpResponse;

/* Sends sBody (JSON, or empty) to sServer + "/api/v1" + sPath and returns the
 * handle for OnlineHttp::Poll.  sToken is the bearer token, or empty. */
int OnlineRequest( const CString &sServer, const CString &sMethod, const CString &sPath, const CString &sBody,
	const CString &sToken, int iTimeout = 30 );

/* The answer's body as JSON; false if it isn't. */
bool OnlineParseBody( const OnlineHttpResponse &r, JsonValue &v );

/* What went wrong with a request, for the player, and the error's code. */
CString OnlineErrorMessage( const OnlineHttpResponse &r, CString *pCode = NULL );

/* "2026-10-12T04:05:06Z" -> seconds since 1970, or 0. */
time_t OnlineParseTime( const CString &s );

#endif
