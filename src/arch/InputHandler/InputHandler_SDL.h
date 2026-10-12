#ifndef INPUT_HANDLER_SDL_KEYBOARD_H
#define INPUT_HANDLER_SDL_KEYBOARD_H

#include "InputHandler.h"

struct SDL_Joystick;

class InputHandler_SDL: public InputHandler
{
	vector<SDL_Joystick *> Joysticks;

	static const char *JoystickName( const char *sName );
	static void SetEventState( bool bEnabled );
	int FindJoystick( uint32_t iInstanceID ) const;
	RageTimer EventTime( uint64_t iTimestampNS, bool bJoystick );

	/* Timestamps from the keyboard and from joysticks are trusted after this many
	 * updates in which they've been seen. */
	enum { NUM_UPDATES_TO_SETTLE = 16 };
	int m_iUpdates;
	int m_iUpdatesWithEvents[2];
	int m_iLastCountedUpdate[2];

	/* The SDL_GetTicksNS time that the previous Update began polling. */
	uint64_t m_iPrevPollNS;
	/* The SDL_GetTicksNS time that this Update finished polling, and RageTimer's idea of the same instant. */
	uint64_t m_iNowNS;
	RageTimer m_Now;

public:
	void Update(float fDeltaTime);
	InputHandler_SDL();
	~InputHandler_SDL();
	void GetDevicesAndDescriptions(vector<InputDevice>& vDevicesOut, vector<CString>& vDescriptionsOut);
};


#endif

/*
 * (c) 2001-2004 Chris Danford, Glenn Maynard
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

