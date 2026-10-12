#include "global.h"
#include "DialogDriver_SDL.h"
#include "ProductInfo.h"

#include <SDL3/SDL.h>

/*
 * SDL_ShowMessageBox doesn't need SDL_Init or a video device, so this works
 * before the window exists, and it runs its own event loop rather than
 * waiting on ours, so it's safe with the game window up.  If there is no
 * display to show it on, we say so on stderr instead.
 *
 * Returns the buttonID of the button pressed, or -1 if the box was closed or
 * couldn't be shown.
 */
static int ShowBox( SDL_MessageBoxFlags flags, const CString &sMessage, const SDL_MessageBoxButtonData *pButtons, int iButtons )
{
	SDL_MessageBoxData data;
	memset( &data, 0, sizeof(data) );
	data.flags = flags;
	data.window = NULL;
	data.title = PRODUCT_NAME;
	data.message = sMessage.c_str();
	data.numbuttons = iButtons;
	data.buttons = pButtons;

	int iButton = -1;
	if( !SDL_ShowMessageBox(&data, &iButton) )
	{
		fprintf( stderr, "%s: %s\n", PRODUCT_NAME, sMessage.c_str() );
		return -1;
	}

	return iButton;
}

static SDL_MessageBoxButtonData MakeButton( int iID, const char *szText, int iFlags )
{
	SDL_MessageBoxButtonData b;
	b.flags = iFlags;
	b.buttonID = iID;
	b.text = szText;
	return b;
}

void DialogDriver_SDL::Error( CString sMessage, CString ID )
{
	SDL_MessageBoxButtonData buttons[] = {
		MakeButton( 0, "OK", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT ),
	};
	ShowBox( SDL_MESSAGEBOX_ERROR, sMessage, buttons, 1 );
}

void DialogDriver_SDL::OK( CString sMessage, CString ID )
{
	/* Only offer to never show it again if it has an ID to remember. */
	SDL_MessageBoxButtonData buttons[] = {
		MakeButton( 0, "OK", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT ),
		MakeButton( 1, "Don't show again", 0 ),
	};
	const int iButton = ShowBox( SDL_MESSAGEBOX_INFORMATION, sMessage, buttons, ID != ""? 2:1 );
	if( iButton == 1 )
		Dialog::IgnoreMessage( ID );
}

Dialog::Result DialogDriver_SDL::AbortRetryIgnore( CString sMessage, CString ID )
{
	SDL_MessageBoxButtonData buttons[] = {
		MakeButton( Dialog::abort, "Abort", 0 ),
		MakeButton( Dialog::retry, "Retry", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT ),
		MakeButton( Dialog::ignore, "Ignore", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT ),
	};
	switch( ShowBox(SDL_MESSAGEBOX_WARNING, sMessage, buttons, 3) )
	{
	case Dialog::abort:	return Dialog::abort;
	case Dialog::retry:	return Dialog::retry;
	default:		return Dialog::ignore;
	}
}

Dialog::Result DialogDriver_SDL::RetryCancel( CString sMessage, CString ID )
{
	SDL_MessageBoxButtonData buttons[] = {
		MakeButton( Dialog::retry, "Retry", SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT ),
		MakeButton( Dialog::cancel, "Cancel", SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT ),
	};
	if( ShowBox(SDL_MESSAGEBOX_WARNING, sMessage, buttons, 2) == Dialog::retry )
		return Dialog::retry;
	return Dialog::cancel;
}
