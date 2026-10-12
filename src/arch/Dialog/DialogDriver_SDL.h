#ifndef DIALOG_BOX_DRIVER_SDL_H
#define DIALOG_BOX_DRIVER_SDL_H

#include "DialogDriver.h"

class DialogDriver_SDL: public DialogDriver
{
public:
	void Error( CString sMessage, CString ID );
	void OK( CString sMessage, CString ID );
	Dialog::Result AbortRetryIgnore( CString sMessage, CString ID );
	Dialog::Result RetryCancel( CString sMessage, CString ID );
};

#define HAVE_DIALOG_SDL

#endif
