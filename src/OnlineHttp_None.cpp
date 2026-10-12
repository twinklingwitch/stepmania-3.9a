/* OnlineHttp without a backend: the game was built without online support
 * (no libcurl).  Every request fails at once. */
#include "global.h"
#include "OnlineHttp.h"

bool OnlineHttp::IsBuiltIn() { return false; }

void OnlineHttpBackendInit() { }
void *OnlineHttpBackendCreate() { return NULL; }
void OnlineHttpBackendDestroy( void * ) { }

void OnlineHttpBackendPerform( void *, const OnlineHttpRequest &, const CString &,
	const volatile bool *, OnlineHttpResponse &out )
{
	out = OnlineHttpResponse();
	out.sError = "this build of the game has no online support";
}
