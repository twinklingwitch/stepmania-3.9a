#include "global.h"
#include "ChatOverlay.h"
#include "OnlineManager.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "ScreenEdit.h"
#include "ScreenManager.h"
#include "ThemeManager.h"
#include "WifeOverlay.h"

#include <time.h>

/* Where things go, in screen coordinates (640x480): a box over the bottom
 * half, the log on the left, who's online on the right, the line being typed
 * at the bottom. */
static const float BOX_LEFT = 16, BOX_RIGHT = SCREEN_WIDTH-16, BOX_TOP = 216, BOX_BOTTOM = SCREEN_HEIGHT-12;
static const float ONLINE_LEFT = BOX_RIGHT - 120;
static const float LOG_LEFT = BOX_LEFT + 10, LOG_RIGHT = ONLINE_LEFT - 12;
static const float LOG_TOP = BOX_TOP + 26, LOG_BOTTOM = BOX_BOTTOM - 26;
static const float ENTRY_Y = BOX_BOTTOM - 8;
static const float LINE_ZOOM = 0.5f;
static const unsigned MAX_ENTRY = 200;	// the server's limit

static const RageColor COLOR_TEXT( 1, 1, 1, 1 );
static const RageColor COLOR_MINE( 1, 0.95f, 0.6f, 1 );
static const RageColor COLOR_ACTION( 0.85f, 0.65f, 1, 1 );
static const RageColor COLOR_JOIN( 0.55f, 0.8f, 0.55f, 1 );
static const RageColor COLOR_PART( 0.6f, 0.6f, 0.6f, 1 );

ChatOverlay::ChatOverlay()
{
	m_bInit = false;
	m_bOpen = false;
	m_fCursorBlink = 0;
	m_iSerial = -1;
	m_bCouldChat = false;
	m_iLinesShown = 0;
}

static void LoadText( BitmapText &t, const CString &sFont, float fZoom, Actor::HorizAlign h, Actor::VertAlign v )
{
	t.LoadFromFont( sFont );
	t.SetHorizAlign( h );
	t.SetVertAlign( v );
	t.SetZoom( fZoom );
	t.SetShadowLength( 0 );
}

void ChatOverlay::Init()
{
	m_bInit = true;
	const CString sFont = THEME->GetPathToF( "Common normal" );

	m_Back.StretchTo( RectF(BOX_LEFT, BOX_TOP, BOX_RIGHT, BOX_BOTTOM) );
	m_Back.SetDiffuse( RageColor(0,0,0,0.85f) );
	m_Divider.StretchTo( RectF(ONLINE_LEFT-6, LOG_TOP, ONLINE_LEFT-5, BOX_BOTTOM-8) );
	m_Divider.SetDiffuse( RageColor(1,1,1,0.25f) );

	LoadText( m_textTitle, sFont, 0.6f, Actor::align_left, Actor::align_top );
	m_textTitle.SetXY( BOX_LEFT+10, BOX_TOP+6 );
	LoadText( m_textHint, sFont, 0.45f, Actor::align_right, Actor::align_top );
	m_textHint.SetXY( BOX_RIGHT-10, BOX_TOP+8 );
	m_textHint.SetDiffuse( RageColor(0.7f,0.7f,0.7f,1) );
	m_textHint.SetText( "Enter: send   /me: action   Esc or F8: close" );
	LoadText( m_textStatus, sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	m_textStatus.SetXY( LOG_LEFT, LOG_TOP );
	m_textStatus.SetDiffuse( RageColor(1,0.5f,0.5f,1) );

	for( int i = 0; i < NUM_LINES; ++i )
		LoadText( m_textLine[i], sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	LoadText( m_textOnline, sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	m_textOnline.SetXY( ONLINE_LEFT, LOG_TOP );
	m_textOnline.SetMaxWidth( (BOX_RIGHT-10 - ONLINE_LEFT) / LINE_ZOOM );
	LoadText( m_textEntry, sFont, 0.55f, Actor::align_left, Actor::align_bottom );
	m_textEntry.SetXY( LOG_LEFT, ENTRY_Y );
}

void ChatOverlay::SetOpen( bool bOpen )
{
	m_bOpen = bOpen;
	ONLINE->SetChatOpen( bOpen );
	if( bOpen )
	{
		if( !m_bInit )
			Init();
		m_iSerial = -1;
		UpdateEntry();
	}
}

/* The log, newest at the bottom, as much of it as fits. */
void ChatOverlay::Rebuild()
{
	m_iSerial = ONLINE->GetChatSerial();
	m_bCouldChat = ONLINE->CanChat();

	const vector<CString> &online = ONLINE->GetChatOnline();
	if( m_bCouldChat )
		m_textTitle.SetText( ssprintf("Chat as %s  -  %u online", ONLINE->GetChatUser().c_str(), (unsigned) online.size()) );
	else
		m_textTitle.SetText( "Chat" );
	m_textOnline.SetText( join("\n", online) );

	CString sStatus = ONLINE->GetChatStatus();
	if( !m_bCouldChat )
		sStatus = "Log in to chat: F7 on the music select screen.";
	m_textStatus.SetText( sStatus, "", (int) ((LOG_RIGHT-LOG_LEFT) / LINE_ZOOM) );
	float fTop = LOG_TOP;
	if( !sStatus.empty() )
		fTop += m_textStatus.GetZoomedHeight() + 4;

	const vector<OnlineChatLine> &lines = ONLINE->GetChatLines();
	const CString sMe = ONLINE->GetChatUser();
	float fBottom = LOG_BOTTOM;
	m_iLinesShown = 0;
	for( int i = (int) lines.size()-1; i >= 0 && m_iLinesShown < NUM_LINES; --i )
	{
		const OnlineChatLine &l = lines[i];
		CString sTime;
		if( l.tAt )
		{
			char szTime[16];
			strftime( szTime, sizeof(szTime), "%H:%M", localtime(&l.tAt) );
			sTime = ssprintf( "[%s] ", szTime );
		}

		CString sText;
		RageColor color = COLOR_TEXT;
		switch( l.kind )
		{
		case OnlineChatLine::MESSAGE:
			sText = ssprintf( "<%s> %s", l.sUser.c_str(), l.sText.c_str() );
			if( l.sUser.CompareNoCase(sMe) == 0 )
				color = COLOR_MINE;
			break;
		case OnlineChatLine::ACTION:
			sText = ssprintf( "* %s %s", l.sUser.c_str(), l.sText.c_str() );
			color = COLOR_ACTION;
			break;
		case OnlineChatLine::JOIN:
			sText = ssprintf( "--> %s is online", l.sUser.c_str() );
			color = COLOR_JOIN;
			break;
		case OnlineChatLine::PART:
			sText = ssprintf( "<-- %s left", l.sUser.c_str() );
			color = COLOR_PART;
			break;
		}

		BitmapText &t = m_textLine[m_iLinesShown];
		t.SetText( sTime + sText, "", (int) ((LOG_RIGHT-LOG_LEFT) / LINE_ZOOM) );
		const float fY = fBottom - t.GetZoomedHeight();
		if( fY < fTop )
			break;
		t.SetXY( LOG_LEFT, fY );
		t.SetDiffuse( color );
		fBottom = fY;
		++m_iLinesShown;
	}
}

/* The line being typed, scrolled so its end shows. */
void ChatOverlay::UpdateEntry()
{
	const bool bCursor = fmodf( m_fCursorBlink, 1.0f ) < 0.5f;
	const CString sCursor = bCursor? "_": " ";
	CString sShown = m_sEntry;
	const float fMax = LOG_RIGHT - LOG_LEFT;
	m_textEntry.SetText( "> " + sShown + sCursor );
	while( !sShown.empty() && m_textEntry.GetZoomedWidth() > fMax )
	{
		sShown.erase( 0, 1 );
		m_textEntry.SetText( "> ..." + sShown + sCursor );
	}
	m_textEntry.SetDiffuse( m_bCouldChat? RageColor(1,1,1,1): RageColor(1,1,1,0.4f) );
}

void ChatOverlay::Update( float fDeltaTime )
{
	if( m_bOpen && !ONLINE->IsEnabled() )
		SetOpen( false );
	if( !m_bOpen )
		return;

	if( m_iSerial != ONLINE->GetChatSerial() || m_bCouldChat != ONLINE->CanChat() )
		Rebuild();

	const bool bWasOn = fmodf( m_fCursorBlink, 1.0f ) < 0.5f;
	m_fCursorBlink += fDeltaTime;
	if( bWasOn != (fmodf(m_fCursorBlink, 1.0f) < 0.5f) )
		UpdateEntry();
}

void ChatOverlay::DrawPrimitives()
{
	if( !m_bOpen )
		return;
	m_Back.Draw();
	m_Divider.Draw();
	m_textTitle.Draw();
	m_textHint.Draw();
	if( !m_textStatus.GetText().empty() )
		m_textStatus.Draw();
	for( int i = 0; i < m_iLinesShown; ++i )
		m_textLine[i].Draw();
	m_textOnline.Draw();
	m_textEntry.Draw();
}

bool ChatOverlay::Input( const DeviceInput &DeviceI, InputEventType type )
{
	if( DeviceI.device != DEVICE_KEYBOARD || type == IET_RELEASE || type == IET_LEVEL_CHANGED )
		return false;

	if( !m_bOpen )
	{
		if( DeviceI.button != KEY_F8 || type != IET_FIRST_PRESS || !ONLINE->IsEnabled() )
			return false;
		if( dynamic_cast<ScreenEdit *>(SCREENMAN->GetTopScreen()) != NULL )
			return false;
		SetOpen( true );
		return true;
	}

	switch( DeviceI.button )
	{
	case KEY_F8:
	case KEY_ESC:
		if( type == IET_FIRST_PRESS )
			SetOpen( false );
		return true;
	case KEY_ENTER:
	case KEY_KP_ENTER:
		if( type != IET_FIRST_PRESS )
			return true;
		{
			CString sText = m_sEntry;
			TrimLeft( sText );
			TrimRight( sText );
			if( !sText.empty() && ONLINE->CanChat() && ONLINE->ChatSay(sText) )
				m_sEntry = "";
		}
		break;
	case KEY_BACK:
		if( !m_sEntry.empty() )
			m_sEntry.erase( m_sEntry.size()-1 );
		break;
	default:
		{
			const char c = TypedChar( DeviceI );
			if( c != '\0' && m_sEntry.size() < MAX_ENTRY )
				m_sEntry += c;
		}
	}
	m_fCursorBlink = 0;
	UpdateEntry();
	return true;
}
