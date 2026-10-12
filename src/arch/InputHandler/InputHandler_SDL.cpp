/*
 * This input handler checks for SDL device input messages and sends them off to
 * InputFilter.  If we add mouse support, it should go here, too.
 *
 * Note that the SDL video event systems are interlinked--you can't use events
 * until video is initialized.  So, if we aren't using SDL for video, we can't
 * use SDL events at all, and so we lose input support, too.  In that case, you'll
 * need to write other input handlers for those cases and load them instead of this.
 * (Part of this is probably because, in Windows, you need a window to get input.)
 */
#include "global.h"
#include "InputHandler_SDL.h"
#include "SDL_utils.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "RageDisplay.h"


/* The values this returns for each key match what the old SDL 1.2 mapping gave,
 * so keymaps that were saved with it keep working. */
static RageKeySym SDLSymToKeySym( SDL_Keycode key )
{
#define KEY_INV KEY_INVALID
	const RageKeySym ASCIIKeySyms[] =
	{
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_INV,	/* 0-3 */
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_INV,	/* 4-7 */
		KEY_BACK,	KEY_TAB,	KEY_INV,	KEY_INV,	/* 8-11 */
		KEY_INV,	KEY_ENTER,	KEY_INV,	KEY_INV,	/* 12-15 */
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_PAUSE,	/* 16-19 */
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_INV,	/* 20-23 */
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_ESC,	/* 24-27 */
		KEY_INV,	KEY_INV,	KEY_INV,	KEY_INV,	/* 28-31 */
		KEY_SPACE,	KEY_EXCL,	KEY_QUOTE,	KEY_HASH,	/* 32-35 */
		KEY_DOLLAR,	KEY_PERCENT,KEY_AMPER,	KEY_SQUOTE,	/* 36-39 */
		KEY_LPAREN,	KEY_RPAREN,	KEY_ASTERISK,KEY_PLUS,	/* 40-43 */
		KEY_COMMA,	KEY_HYPHEN,	KEY_PERIOD,	KEY_SLASH,	/* 44-47 */
		KEY_C0,		KEY_C1,		KEY_C2,		KEY_C3,		/* 48-51 */
		KEY_C4,		KEY_C5,		KEY_C6,		KEY_C7,		/* 52-55 */
		KEY_C8,		KEY_C9,		KEY_COLON,	KEY_SEMICOLON,	/* 56-59 */
		KEY_LANGLE,	KEY_EQUAL,	KEY_RANGLE,	KEY_QUESTION,	/* 60-63 */
		KEY_AT, KEY_CA, KEY_CB, KEY_CC, KEY_CD, KEY_CE, KEY_CF, KEY_CG, KEY_CH, /* 64-72 */
		KEY_CI, KEY_CJ, KEY_CK, KEY_CL, KEY_CM, KEY_CN, KEY_CO, KEY_CP, KEY_CQ, /* 73-81 */
		KEY_CR, KEY_CS, KEY_CT, KEY_CU, KEY_CV, KEY_CW, KEY_CX, KEY_CY, KEY_CZ, /* 82-90 */
		KEY_LBRACKET, KEY_BACKSLASH, KEY_RBRACKET, KEY_CARAT, /* 91-94 */
		KEY_UNDERSCORE, KEY_ACCENT, KEY_Ca, KEY_Cb,		/* 95-98 */
		KEY_Cc, KEY_Cd, KEY_Ce, KEY_Cf, KEY_Cg, KEY_Ch, KEY_Ci, KEY_Cj, KEY_Ck, /* 99-107 */
		KEY_Cl, KEY_Cm, KEY_Cn, KEY_Co, KEY_Cp, KEY_Cq, KEY_Cr, KEY_Cs, KEY_Ct, /* 108-116 */
		KEY_Cu, KEY_Cv, KEY_Cw, KEY_Cx, KEY_Cy, KEY_Cz, /* 117-122 */
		KEY_LBRACE, KEY_PIPE, KEY_RBRACE, KEY_INV, KEY_DEL /* 123-127 */
	};

	/* 0...127: */
	if( key < ARRAYSIZE(ASCIIKeySyms) )
		return ASCIIKeySyms[key];

	/* Latin-1 keys (SDL 1.2's SDLK_WORLD_0 ... SDLK_WORLD_95) to
	 * KEY_OTHER_0 ... KEY_OTHER_0 + 95 */
	if( key >= 160 && key <= 255 )
		return (RageKeySym) (key - 160 + KEY_OTHER_0);

	/* The keys SDL 1.2 gave values over 255 that we map by number. */
	int iOldSym = -1;

	switch( key )
	{
	/* The keypad digits aren't contiguous. */
	case SDLK_KP_0:			return KEY_KP_C0;
	case SDLK_KP_1:			return KEY_KP_C1;
	case SDLK_KP_2:			return KEY_KP_C2;
	case SDLK_KP_3:			return KEY_KP_C3;
	case SDLK_KP_4:			return KEY_KP_C4;
	case SDLK_KP_5:			return KEY_KP_C5;
	case SDLK_KP_6:			return KEY_KP_C6;
	case SDLK_KP_7:			return KEY_KP_C7;
	case SDLK_KP_8:			return KEY_KP_C8;
	case SDLK_KP_9:			return KEY_KP_C9;
	case SDLK_KP_PERIOD:	return KEY_KP_PERIOD;
	case SDLK_KP_DIVIDE:	return KEY_KP_SLASH;
	case SDLK_KP_MULTIPLY:	return KEY_KP_ASTERISK;
	case SDLK_KP_MINUS:		return KEY_KP_HYPHEN;
	case SDLK_KP_PLUS:		return KEY_KP_PLUS;
	case SDLK_KP_EQUALS:	return KEY_KP_EQUAL;
	case SDLK_KP_ENTER:		return KEY_KP_ENTER;
	case SDLK_UP:			return KEY_UP;
	case SDLK_DOWN:			return KEY_DOWN;
	case SDLK_RIGHT:		return KEY_RIGHT;
	case SDLK_LEFT:			return KEY_LEFT;
	case SDLK_INSERT:		return KEY_INSERT;
	case SDLK_HOME:			return KEY_HOME;
	case SDLK_END:			return KEY_END;
	case SDLK_PAGEUP:		return KEY_PGUP;
	case SDLK_PAGEDOWN:		return KEY_PGDN;
	case SDLK_F1:			return KEY_F1;
	case SDLK_F2:			return KEY_F2;
	case SDLK_F3:			return KEY_F3;
	case SDLK_F4:			return KEY_F4;
	case SDLK_F5:			return KEY_F5;
	case SDLK_F6:			return KEY_F6;
	case SDLK_F7:			return KEY_F7;
	case SDLK_F8:			return KEY_F8;
	case SDLK_F9:			return KEY_F9;
	case SDLK_F10:			return KEY_F10;
	case SDLK_F11:			return KEY_F11;
	case SDLK_F12:			return KEY_F12;
	case SDLK_F13:			return KEY_F13;
	case SDLK_F14:			return KEY_F14;
	case SDLK_F15:			return KEY_F15;

	case SDLK_PAUSE:		return KEY_PAUSE;
	case SDLK_NUMLOCKCLEAR:	return KEY_NUMLOCK;
	case SDLK_CAPSLOCK:		return KEY_CAPSLOCK;
	case SDLK_SCROLLLOCK:	return KEY_SCRLLOCK;
	case SDLK_SYSREQ:		return KEY_PRTSC;
	case SDLK_PRINTSCREEN:	return KEY_PRTSC;
	case SDLK_RSHIFT:		return KEY_RSHIFT;
	case SDLK_LSHIFT:		return KEY_LSHIFT;
	case SDLK_RCTRL:		return KEY_RCTRL;
	case SDLK_LCTRL:		return KEY_LCTRL;
	case SDLK_RALT:			return KEY_RALT;
	case SDLK_LALT:			return KEY_LALT;
#if defined(DARWIN)
	/* SDL 1.2 called the Mac's Command keys meta, and so do Cmd+Q and Cmd+F12. */
	case SDLK_LGUI:			return KEY_LMETA;
	case SDLK_RGUI:			return KEY_RMETA;
#else
	case SDLK_LGUI:			return KEY_LSUPER;
	case SDLK_RGUI:			return KEY_RSUPER;
#endif
	case SDLK_APPLICATION:	return KEY_MENU;
	case SDLK_MENU:			return KEY_MENU;

	case SDLK_MODE:			iOldSym = 313; break;
	case SDLK_HELP:			iOldSym = 315; break;
	case SDLK_POWER:		iOldSym = 320; break;
	case SDLK_UNDO:			iOldSym = 322; break;
	}

	/* Map anything else to key+KEY_OTHER_0+95. */
	if( iOldSym != -1 && iOldSym+KEY_OTHER_0+95 < KEY_LAST_OTHER )
		return (RageKeySym) (iOldSym+KEY_OTHER_0+95);

	return KEY_INVALID;
}

InputHandler_SDL::InputHandler_SDL()
{
	m_iPrevPollNS = m_iNowNS = SDL_GetTicksNS();
	m_iUpdates = 0;
	for( int i = 0; i < 2; ++i )
	{
		m_iUpdatesWithEvents[i] = 0;
		m_iLastCountedUpdate[i] = 0;
	}

	if( !SDL_InitSubSystem(SDL_INIT_JOYSTICK) )
		LOG->Warn( "SDL_INIT_JOYSTICK failed: %s", mySDL_GetError().c_str() );

	/* We can do this to get Unicode values in the key struct, which (with
	 * a little more work) will make us work better on international keyboards.
	 * Not all archs support this well. */
	// SDL_StartTextInput( window );

	//
	// Init joysticks
	//
	int iJoysticksFound = 0;
	SDL_JoystickID *pJoystickIDs = SDL_GetJoysticks( &iJoysticksFound );
	int iNumJoySticks = min( iJoysticksFound, NUM_JOYSTICKS );
	LOG->Info( "Found %d joysticks", iNumJoySticks );
	for( int i=0; i<iNumJoySticks; i++ )
	{
		SDL_Joystick *pJoystick = SDL_OpenJoystick( pJoystickIDs[i] );

		if(pJoystick == NULL) {
			LOG->Info("   %d: '%s' Error opening: %s",
				i, JoystickName(SDL_GetJoystickNameForID(pJoystickIDs[i])), SDL_GetError());

			/* Keep the slot, so the devices after this one keep their numbers. */
			Joysticks.push_back( NULL );
			continue;
		}

		LOG->Info( "   %d: '%s' axes: %d, hats: %d, buttons: %d",
			i,
			JoystickName(SDL_GetJoystickName(pJoystick)),
			SDL_GetNumJoystickAxes(pJoystick),
			SDL_GetNumJoystickHats(pJoystick),
			SDL_GetNumJoystickButtons(pJoystick) );

		/* For some weird reason, we won't get any joystick events at all
		 * if we don't keep the joystick open.  (Why?  The joystick event
		 * API is completely separate from the SDL_Joystick polling API ...) */
		Joysticks.push_back(pJoystick);
	}
	SDL_free( pJoystickIDs );

	SetEventState( true );
}

InputHandler_SDL::~InputHandler_SDL()
{
	for( unsigned i = 0; i < Joysticks.size(); ++i )
		if( Joysticks[i] != NULL )
			SDL_CloseJoystick(Joysticks[i]);

	SDL_QuitSubSystem( SDL_INIT_JOYSTICK );

	SetEventState( false );
}

const char *InputHandler_SDL::JoystickName( const char *sName )
{
	return sName? sName:"";
}

void InputHandler_SDL::SetEventState( bool bEnabled )
{
	for( Uint32 i = SDL_EVENT_KEY_DOWN; i <= SDL_EVENT_KEY_UP; ++i )
		SDL_SetEventEnabled( i, bEnabled );

	static const Uint32 JoyEvents[] = {
		SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_HAT_MOTION,
		SDL_EVENT_JOYSTICK_BUTTON_DOWN, SDL_EVENT_JOYSTICK_BUTTON_UP
	};
	for( unsigned i = 0; i < ARRAYSIZE(JoyEvents); ++i )
		SDL_SetEventEnabled( JoyEvents[i], bEnabled );
}

/* Events identify a joystick by instance ID, which isn't the device number.
 * Return the device number, or -1 if it isn't one of ours. */
int InputHandler_SDL::FindJoystick( uint32_t iInstanceID ) const
{
	for( unsigned i = 0; i < Joysticks.size(); ++i )
		if( Joysticks[i] != NULL && SDL_GetJoystickID(Joysticks[i]) == iInstanceID )
			return i;
	return -1;
}

/* Convert the timestamp of an SDL event to a RageTimer, or return RageZeroTimer
 * (which makes InputHandler guess) if it doesn't say when the input happened.
 *
 * Only some of SDL's backends know when the OS got the input: Wayland (the
 * compositor's time) and the Linux evdev joystick driver (the kernel's time).
 * The rest, including X11 and the BSD joystick drivers, stamp events with the time
 * they read them.  SDL's clock isn't RageTimer's, so take the age of the event on
 * SDL's clock and apply it to ours.
 *
 * SDL rebases those timestamps onto its own clock by assuming that the first event
 * was read the moment it happened, and lowers the offset whenever an event turns
 * out to have been read sooner than that.  So the timestamps are never early, but
 * run late by the shortest delay SDL has seen so far, which is only small once it
 * has seen events at a few different times relative to our polling.  Until then,
 * let InputHandler guess.
 *
 * The guess InputHandler makes is halfway between the previous poll and this one,
 * so it can be off by half a frame either way; a believable timestamp is closer. */
RageTimer InputHandler_SDL::EventTime( uint64_t iTimestampNS, bool bJoystick )
{
	/* The keyboard and the joysticks are rebased separately; each needs to settle. */
	const int iClass = bJoystick? 1:0;
	if( m_iLastCountedUpdate[iClass] != m_iUpdates )
	{
		m_iLastCountedUpdate[iClass] = m_iUpdates;
		if( m_iUpdatesWithEvents[iClass] <= NUM_UPDATES_TO_SETTLE )
			++m_iUpdatesWithEvents[iClass];
	}
	if( m_iUpdatesWithEvents[iClass] <= NUM_UPDATES_TO_SETTLE )
		return RageZeroTimer;

	/* A timestamp from when SDL pumped events is the time of the poll, not of the
	 * input; the guess is better. */
	if( iTimestampNS == 0 || mySDL_StampedWhilePumping(iTimestampNS) )
		return RageZeroTimer;

	/* Whatever is queued now happened after the previous poll began.  Don't
	 * believe anything from the future, or from before that (the clocks may have
	 * been stepped, or SDL may have started over with the offset). */
	const uint64_t iSlackNS = 2000000;	// 2 ms; Wayland timestamps are in whole ms
	if( iTimestampNS > m_iNowNS || iTimestampNS + iSlackNS < m_iPrevPollNS )
		return RageZeroTimer;

	const float fAgo = float(m_iNowNS - iTimestampNS) / 1000000000.0f;
	return m_Now + -fAgo;
}

void InputHandler_SDL::Update(float fDeltaTime)
{
	/* Events that arrived while we were updating the game weren't read when
	 * the frame started; get them now, so they're handled this frame. */
	const uint64_t iPollNS = SDL_GetTicksNS();
	mySDL_PumpEvents();
	m_iNowNS = SDL_GetTicksNS();
	m_Now.Touch();
	++m_iUpdates;

	SDL_Event event;
	while( SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP) > 0 ||
		SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_REMOVED) > 0 )
	{
		switch(event.type)
		{
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			{
			/* We don't want key repeat. */
			if( event.key.repeat )
				continue;

			LOG->Trace("key: sym %u, key %i, state %i",
				event.key.key, SDLSymToKeySym(event.key.key), event.key.down );
			DeviceInput di( DEVICE_KEYBOARD, SDLSymToKeySym(event.key.key), 0, EventTime(event.key.timestamp, false) );
			ButtonPressed(di, event.key.down);
			}
			continue;

		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		case SDL_EVENT_JOYSTICK_BUTTON_UP:
		{
			const int iJoy = FindJoystick( event.jbutton.which );
			if( iJoy == -1 )
				continue;
			InputDevice i = InputDevice(DEVICE_JOY1 + iJoy);
			JoystickButton Button = JoystickButton(JOY_1 + event.jbutton.button);
			if(Button >= NUM_JOYSTICK_BUTTONS)
			{
				LOG->Warn("Ignored joystick event (button too high)");
				continue;
			}
			DeviceInput di(i, Button, 0, EventTime(event.jbutton.timestamp, true));
			ButtonPressed(di, event.jbutton.down);
			continue;
		}
		
		case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		{
			const int iJoy = FindJoystick( event.jaxis.which );
			if( iJoy == -1 )
				continue;
			InputDevice i = InputDevice(DEVICE_JOY1 + iJoy);
			JoystickButton neg = (JoystickButton)(JOY_LEFT+2*event.jaxis.axis);
			JoystickButton pos = (JoystickButton)(JOY_RIGHT+2*event.jaxis.axis);
			float l = SCALE( event.jaxis.value, 0.0f, 32768.0f, 0.0f, 1.0f );
			const RageTimer ts = EventTime(event.jaxis.timestamp, true);
			ButtonPressed(DeviceInput(i, neg,max(-l,0),ts), event.jaxis.value < -16000);
			ButtonPressed(DeviceInput(i, pos,max(+l,0),ts), event.jaxis.value > +16000);
			continue;
		}
		
		case SDL_EVENT_JOYSTICK_HAT_MOTION:
		{
			const int iJoy = FindJoystick( event.jhat.which );
			if( iJoy == -1 )
				continue;
			InputDevice i = InputDevice(DEVICE_JOY1 + iJoy);
			const RageTimer ts = EventTime(event.jhat.timestamp, true);
			ButtonPressed(DeviceInput(i, JOY_HAT_UP, 0, ts), !!(event.jhat.value & SDL_HAT_UP));
			ButtonPressed(DeviceInput(i, JOY_HAT_DOWN, 0, ts), !!(event.jhat.value & SDL_HAT_DOWN));
			ButtonPressed(DeviceInput(i, JOY_HAT_LEFT, 0, ts), !!(event.jhat.value & SDL_HAT_LEFT));
			ButtonPressed(DeviceInput(i, JOY_HAT_RIGHT, 0, ts), !!(event.jhat.value & SDL_HAT_RIGHT));
			continue;
		}
		}
	}

	/* Every event queued so far has been read. */
	mySDL_ForgetPumps();
	m_iPrevPollNS = iPollNS;

	InputHandler::UpdateTimer();
}


void InputHandler_SDL::GetDevicesAndDescriptions(vector<InputDevice>& vDevicesOut, vector<CString>& vDescriptionsOut)
{
	vDevicesOut.push_back( DEVICE_KEYBOARD );
	vDescriptionsOut.push_back( "Keyboard" );

	for( unsigned i=0; i<Joysticks.size(); i++ )
	{
		if( Joysticks[i] != NULL )
		{
			vDevicesOut.push_back( InputDevice(DEVICE_JOY1+i) );
			vDescriptionsOut.push_back( JoystickName(SDL_GetJoystickName(Joysticks[i])) );
		}
	}
}

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
