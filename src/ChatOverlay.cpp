#include "global.h"
#include "ChatOverlay.h"
#include "OnlineManager.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "ScreenEdit.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "ThemeManager.h"
#include "WifeOverlay.h"

#include <time.h>

/* Where things go, in screen coordinates (640x480): a box over the bottom
 * half, the lines on the left, a column on the right, the line being typed
 * at the bottom. */
static const float BOX_LEFT = 16, BOX_RIGHT = SCREEN_WIDTH-16, BOX_TOP = 216, BOX_BOTTOM = SCREEN_HEIGHT-12;
static const float SIDE_LEFT = BOX_RIGHT - 120;
static const float LOG_LEFT = BOX_LEFT + 10, LOG_RIGHT = SIDE_LEFT - 12;
static const float LOG_BOTTOM = BOX_BOTTOM - 26;
static const float ENTRY_Y = BOX_BOTTOM - 8;
static const float LINE_ZOOM = 0.5f;
static const float LINE_GAP = 2;
static const unsigned MAX_ENTRY = 200;	// the server's limit

static const RageColor COLOR_TEXT( 1, 1, 1, 1 );
static const RageColor COLOR_MINE( 1, 0.95f, 0.6f, 1 );
static const RageColor COLOR_ACTION( 0.85f, 0.65f, 1, 1 );
static const RageColor COLOR_JOIN( 0.55f, 0.8f, 0.55f, 1 );
static const RageColor COLOR_PART( 0.6f, 0.6f, 0.6f, 1 );
static const RageColor COLOR_SYSTEM( 0.6f, 0.8f, 1, 1 );
static const RageColor COLOR_NOTICE( 1, 0.6f, 0.4f, 1 );

/*
 * ChatPanel
 */

ChatPanel::ChatPanel( RageKeySym OpenKey )
{
	m_OpenKey = OpenKey;
	m_bInit = false;
	m_bOpen = false;
	m_bEntryEnabled = true;
	m_fCursorBlink = 0;
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

void ChatPanel::Init()
{
	m_bInit = true;
	const CString sFont = THEME->GetPathToF( "Common normal" );

	m_Back.StretchTo( RectF(BOX_LEFT, BOX_TOP, BOX_RIGHT, BOX_BOTTOM) );
	m_Back.SetDiffuse( RageColor(0,0,0,0.93f) );
	m_Divider.SetDiffuse( RageColor(1,1,1,0.25f) );

	LoadText( m_textTitle, sFont, 0.6f, Actor::align_left, Actor::align_top );
	m_textTitle.SetXY( BOX_LEFT+10, BOX_TOP+6 );
	m_textTitle.SetMaxWidth( (SIDE_LEFT - BOX_LEFT - 20) / 0.6f );
	LoadText( m_textSub, sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	m_textSub.SetDiffuse( RageColor(0.75f,0.85f,1,1) );
	LoadText( m_textHint, sFont, 0.45f, Actor::align_right, Actor::align_top );
	m_textHint.SetXY( BOX_RIGHT-10, BOX_TOP+8 );
	m_textHint.SetDiffuse( RageColor(0.7f,0.7f,0.7f,1) );
	LoadText( m_textStatus, sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	m_textStatus.SetDiffuse( RageColor(1,0.5f,0.5f,1) );

	for( int i = 0; i < NUM_LINES; ++i )
		LoadText( m_textLine[i], sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	LoadText( m_textSide, sFont, LINE_ZOOM, Actor::align_left, Actor::align_top );
	m_textSide.SetMaxWidth( (BOX_RIGHT-10 - SIDE_LEFT) / LINE_ZOOM );
	LoadText( m_textEntry, sFont, 0.55f, Actor::align_left, Actor::align_bottom );
	m_textEntry.SetXY( LOG_LEFT, ENTRY_Y );
	UpdateEntry();
}

void ChatPanel::SetOpen( bool bOpen )
{
	m_bOpen = bOpen;
	if( bOpen )
	{
		if( !m_bInit )
			Init();
		UpdateEntry();
	}
}

ChatPanel::Line ChatPanel::FormatLine( const OnlineChatLine &l, const CString &sMe )
{
	CString sTime;
	if( l.tAt )
	{
		char szTime[16];
		strftime( szTime, sizeof(szTime), "%H:%M", localtime(&l.tAt) );
		sTime = ssprintf( "[%s] ", szTime );
	}

	switch( l.kind )
	{
	case OnlineChatLine::MESSAGE:
		return Line( sTime + ssprintf("<%s> %s", l.sUser.c_str(), l.sText.c_str()),
			l.sUser.CompareNoCase(sMe) == 0? COLOR_MINE: COLOR_TEXT );
	case OnlineChatLine::ACTION:
		return Line( sTime + ssprintf("* %s %s", l.sUser.c_str(), l.sText.c_str()), COLOR_ACTION );
	case OnlineChatLine::JOIN:
		return Line( sTime + ssprintf("--> %s is online", l.sUser.c_str()), COLOR_JOIN );
	case OnlineChatLine::PART:
		return Line( sTime + ssprintf("<-- %s left", l.sUser.c_str()), COLOR_PART );
	case OnlineChatLine::SYSTEM:
		return Line( sTime + "*** " + l.sText, COLOR_SYSTEM );
	case OnlineChatLine::NOTICE:
	default:
		return Line( sTime + "-!- " + l.sText, COLOR_NOTICE );
	}
}

void ChatPanel::SetTexts( const CString &sTitle, const CString &sSub, const CString &sHint, const CString &sStatus, const CString &sSide )
{
	if( !m_bInit )
		Init();
	m_textTitle.SetText( sTitle );
	m_textSub.SetText( sSub, "", (int) ((LOG_RIGHT-LOG_LEFT) / LINE_ZOOM) );
	m_textHint.SetText( sHint );
	m_textStatus.SetText( sStatus, "", (int) ((LOG_RIGHT-LOG_LEFT) / LINE_ZOOM) );
	m_textSide.SetText( sSide );
	Layout();
}

void ChatPanel::SetLines( const vector<Line> &lines )
{
	if( !m_bInit )
		Init();
	m_Lines = lines;
	Layout();
}

void ChatPanel::SetEntryEnabled( bool bEnabled )
{
	if( m_bEntryEnabled == bEnabled )
		return;
	m_bEntryEnabled = bEnabled;
	if( m_bInit )
		UpdateEntry();
}

/* The title, then what's under it, then as many of the newest lines as fit. */
void ChatPanel::Layout()
{
	float fTop = BOX_TOP + 26;
	m_textSub.SetXY( LOG_LEFT, fTop );
	if( !m_textSub.GetText().empty() )
		fTop += m_textSub.GetZoomedHeight() + 4;
	m_textStatus.SetXY( LOG_LEFT, fTop );
	if( !m_textStatus.GetText().empty() )
		fTop += m_textStatus.GetZoomedHeight() + 4;
	m_textSide.SetXY( SIDE_LEFT, BOX_TOP + 26 );
	m_Divider.StretchTo( RectF(SIDE_LEFT-6, BOX_TOP + 26, SIDE_LEFT-5, BOX_BOTTOM-8) );

	float fBottom = LOG_BOTTOM;
	m_iLinesShown = 0;
	for( int i = (int) m_Lines.size()-1; i >= 0 && m_iLinesShown < NUM_LINES; --i )
	{
		BitmapText &t = m_textLine[m_iLinesShown];
		t.SetText( m_Lines[i].sText, "", (int) ((LOG_RIGHT-LOG_LEFT) / LINE_ZOOM) );
		const float fY = fBottom - t.GetZoomedHeight() - LINE_GAP;
		if( fY < fTop )
			break;
		t.SetXY( LOG_LEFT, fY );
		t.SetDiffuse( m_Lines[i].color );
		fBottom = fY;
		++m_iLinesShown;
	}
}

/* The line being typed, scrolled so its end shows. */
void ChatPanel::UpdateEntry()
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
	m_textEntry.SetDiffuse( m_bEntryEnabled? RageColor(1,1,1,1): RageColor(1,1,1,0.4f) );
}

/* The editor and gameplay have every function key for themselves (F8 is
 * AutoPlay in gameplay, F10 the global offset), and nobody should be typing
 * there anyway. */
bool ChatPanel::CanOpenHere()
{
	const Screen *pTop = SCREENMAN->GetTopScreen();
	return dynamic_cast<const ScreenEdit *>( pTop ) == NULL && dynamic_cast<const ScreenGameplay *>( pTop ) == NULL;
}

void ChatPanel::UpdatePanel( float fDeltaTime )
{
	if( m_bOpen && (!ONLINE->IsEnabled() || !CanOpenHere()) )
		SetOpen( false );
	if( !m_bOpen )
		return;
	const bool bWasOn = fmodf( m_fCursorBlink, 1.0f ) < 0.5f;
	m_fCursorBlink += fDeltaTime;
	if( bWasOn != (fmodf(m_fCursorBlink, 1.0f) < 0.5f) )
		UpdateEntry();
}

void ChatPanel::DrawPrimitives()
{
	if( !m_bOpen )
	{
		DrawClosed();
		return;
	}
	m_Back.Draw();
	m_Divider.Draw();
	m_textTitle.Draw();
	m_textHint.Draw();
	if( !m_textSub.GetText().empty() )
		m_textSub.Draw();
	if( !m_textStatus.GetText().empty() )
		m_textStatus.Draw();
	for( int i = 0; i < m_iLinesShown; ++i )
		m_textLine[i].Draw();
	m_textSide.Draw();
	m_textEntry.Draw();
}

bool ChatPanel::Input( const DeviceInput &DeviceI, InputEventType type )
{
	if( DeviceI.device != DEVICE_KEYBOARD || type == IET_LEVEL_CHANGED )
		return false;
	/* The screen mustn't see a key let go that it never saw pressed: Esc
	 * goes back on the music select screen even then. */
	if( type == IET_RELEASE )
		return m_KeysDown.erase( DeviceI.button ) != 0;

	if( !m_bOpen )
	{
		if( DeviceI.button != m_OpenKey || type != IET_FIRST_PRESS || !ONLINE->IsEnabled() )
			return false;
		if( !CanOpenHere() )
			return false;
		SetOpen( true );
		m_KeysDown.insert( DeviceI.button );
		return true;
	}

	/* A key that was down before we opened is the screen's until it's let go. */
	if( type != IET_FIRST_PRESS && !m_KeysDown.count(DeviceI.button) )
		return false;

	switch( DeviceI.button )
	{
	case KEY_F8:
	case KEY_F10:
		if( DeviceI.button != m_OpenKey )
		{
			/* Another panel's key: it opens in our place. */
			SetOpen( false );
			return false;
		}
		/* fall through */
	case KEY_ESC:
		m_KeysDown.insert( DeviceI.button );
		if( type == IET_FIRST_PRESS )
			SetOpen( false );
		return true;
	case KEY_ENTER:
	case KEY_KP_ENTER:
		m_KeysDown.insert( DeviceI.button );
		if( type != IET_FIRST_PRESS )
			return true;
		{
			CString sText = m_sEntry;
			TrimLeft( sText );
			TrimRight( sText );
			if( !sText.empty() && m_bEntryEnabled && Submit(sText) )
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
	m_KeysDown.insert( DeviceI.button );
	m_fCursorBlink = 0;
	UpdateEntry();
	return true;
}

/*
 * ChatOverlay
 */

ChatOverlay::ChatOverlay(): ChatPanel( KEY_F8 )
{
	m_iSerial = -1;
	m_bCouldChat = false;
}

void ChatOverlay::SetOpen( bool bOpen )
{
	ChatPanel::SetOpen( bOpen );
	ONLINE->SetChatOpen( bOpen );
	m_iSerial = -1;
}

void ChatOverlay::Rebuild()
{
	m_iSerial = ONLINE->GetChatSerial();
	m_bCouldChat = ONLINE->CanChat();

	const vector<CString> &online = ONLINE->GetChatOnline();
	CString sTitle = "Chat";
	if( m_bCouldChat )
		sTitle = ssprintf( "Chat as %s  -  %u online", ONLINE->GetChatUser().c_str(), (unsigned) online.size() );
	CString sStatus = ONLINE->GetChatStatus();
	if( !m_bCouldChat )
		sStatus = "Log in to chat: F7 on the music select screen.";
	SetTexts( sTitle, "", "Enter: send   /me: action   Esc or F8: close", sStatus, join("\n", online) );

	const vector<OnlineChatLine> &lines = ONLINE->GetChatLines();
	vector<Line> out;
	for( unsigned i = 0; i < lines.size(); ++i )
		out.push_back( FormatLine(lines[i], ONLINE->GetChatUser()) );
	SetLines( out );
	SetEntryEnabled( m_bCouldChat );
}

void ChatOverlay::Update( float fDeltaTime )
{
	UpdatePanel( fDeltaTime );
	if( !IsOpen() )
		return;
	if( m_iSerial != ONLINE->GetChatSerial() || m_bCouldChat != ONLINE->CanChat() )
		Rebuild();
}

bool ChatOverlay::Submit( const CString &sText )
{
	return ONLINE->CanChat() && ONLINE->ChatSay( sText );
}
