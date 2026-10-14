#include "global.h"
#include "WifeOverlay.h"
#include "ChartInfo.h"
#include "ChartMSD.h"
#include "GameState.h"
#include "OnlineManager.h"
#include "PrefsManager.h"
#include "ProfileManager.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "ScreenEvaluation.h"
#include "ScreenGameplay.h"
#include "ScreenManager.h"
#include "ScreenSelectMusic.h"
#include "ThemeManager.h"
#include "WifeManager.h"
#include "song.h"
#include "Steps.h"

#include <ctype.h>

/* Where things go.  Everything is in screen coordinates (640x480). */
static const float PANEL_LEFT = 40, PANEL_RIGHT = 600, PANEL_TOP = 70;
static const float TEXT_ZOOM = 0.6f;
static const float ROW_Y = 128;
static const int NUM_ROWS = 10;

/* The skillsets are in a column on the right of the panel, the leaderboard (or
 * a text) in the rest.  Two tables, for the two players' SSRs on the results
 * screen, share the column. */
static const float TABLE_RIGHT = PANEL_RIGHT - 14;
static const float TABLE_LEFT_ONE = 446;
static const float TABLE_LEFT_TWO[2] = { 330, 462 };
static const float TABLE_RIGHT_TWO[2] = { 454, TABLE_RIGHT };

/* The leaderboard's columns. */
static const float RANK_RIGHT = PANEL_LEFT + 32;
static const float WIFE_RIGHT = PANEL_LEFT + 100;
static const float NAME_LEFT = PANEL_LEFT + 108;
static const float NAME_WIDTH = 140;
static const float INFO_LEFT = NAME_LEFT + NAME_WIDTH + 10;

/* Skillsets this far under Overall are dimmed a little. */
static const float DIM_BELOW = 0.8f;

/* How long the selection must stay put before its MSD is worked out, so
 * scrolling through the wheel doesn't run the calculator for every chart. */
static const float MSD_SETTLE_SECONDS = 0.25f;

/* The counters go in the bottom bar of the default theme's gameplay screen, between
 * the two score boxes and below where the notes go, each on its player's side. */
static const float COUNTER_Y = 441;
static const float COUNTER_X[NUM_PLAYERS] = { 254, SCREEN_WIDTH-254 };

/* How long the rate popup stays, and how much of that it fades out in. */
static const float POPUP_SECONDS = 1.5f;
static const float POPUP_FADE_SECONDS = 0.3f;
static const float POPUP_Y = 36;
static const float POPUP_ZOOM = 0.9f;

WifeOverlay::WifeOverlay()
{
	m_bInit = false;
	m_bPopupInit = false;
	m_fPopupLeft = 0;
	m_bShowSelect = false;
	m_bAllProfiles = false;
	m_bShowEval = false;
	m_bShowOnline = false;
	m_OnlinePlayer = PLAYER_1;
	m_bFormOpen = false;
	m_bFormOnPassword = false;
	m_pSelectSteps = NULL;
	m_fSelectRate = 0;
	m_bSelectAll = false;
	m_fSettled = 0;
	m_bMSDPending = false;
	m_bPanelVisible = false;
	m_bPanelIsBody = false;
	m_fRowY = ROW_Y;
	m_fPanelBottom = PANEL_TOP + 100;
	m_iTables = 0;
	m_fTablesBottom = ROW_Y;
	FOREACH_PlayerNumber( p )
		m_bCounterVisible[p] = false;
}

/* The fonts are loaded the first time they're needed, so nothing happens when
 * Wife is off. */
void WifeOverlay::Init()
{
	m_bInit = true;

	const CString sFont = THEME->GetPathToF( "Common normal" );

	m_PanelBack.SetDiffuse( RageColor(0,0,0,0.82f) );

	m_textTitle.LoadFromFont( sFont );
	m_textTitle.SetHorizAlign( Actor::align_left );
	m_textTitle.SetVertAlign( Actor::align_top );
	m_textTitle.SetXY( PANEL_LEFT+14, PANEL_TOP+10 );
	m_textTitle.SetZoom( 0.8f );
	m_textTitle.SetMaxWidth( (PANEL_RIGHT-PANEL_LEFT-28) / 0.8f );

	m_textSubtitle.LoadFromFont( sFont );
	m_textSubtitle.SetHorizAlign( Actor::align_left );
	m_textSubtitle.SetVertAlign( Actor::align_top );
	m_textSubtitle.SetXY( PANEL_LEFT+14, PANEL_TOP+34 );
	m_textSubtitle.SetZoom( TEXT_ZOOM );
	m_textSubtitle.SetDiffuse( RageColor(0.75f,0.85f,1,1) );
	m_textSubtitle.SetMaxWidth( (PANEL_RIGHT-PANEL_LEFT-28) / TEXT_ZOOM );

	m_textHint.LoadFromFont( sFont );
	m_textHint.SetHorizAlign( Actor::align_left );
	m_textHint.SetVertAlign( Actor::align_bottom );
	m_textHint.SetZoom( 0.5f );
	m_textHint.SetDiffuse( RageColor(0.7f,0.7f,0.7f,1) );

	m_textMeasure.LoadFromFont( sFont );
	m_textMeasure.SetZoom( TEXT_ZOOM );

	m_textMessage.LoadFromFont( sFont );
	m_textMessage.SetHorizAlign( Actor::align_left );
	m_textMessage.SetVertAlign( Actor::align_top );
	m_textMessage.SetZoom( TEXT_ZOOM );

	m_textRanks.LoadFromFont( sFont );
	m_textRanks.SetHorizAlign( Actor::align_right );
	m_textRanks.SetVertAlign( Actor::align_top );
	m_textRanks.SetZoom( TEXT_ZOOM );

	m_textWifes.LoadFromFont( sFont );
	m_textWifes.SetHorizAlign( Actor::align_right );
	m_textWifes.SetVertAlign( Actor::align_top );
	m_textWifes.SetZoom( TEXT_ZOOM );

	m_textNames.LoadFromFont( sFont );
	m_textNames.SetHorizAlign( Actor::align_left );
	m_textNames.SetVertAlign( Actor::align_top );
	m_textNames.SetZoom( TEXT_ZOOM );
	m_textNames.SetMaxWidth( NAME_WIDTH / TEXT_ZOOM );

	m_textInfo.LoadFromFont( sFont );
	m_textInfo.SetHorizAlign( Actor::align_left );
	m_textInfo.SetVertAlign( Actor::align_top );
	m_textInfo.SetZoom( TEXT_ZOOM );
	m_textInfo.SetDiffuse( RageColor(0.8f,0.8f,0.8f,1) );
	m_textInfo.SetMaxWidth( (TABLE_LEFT_ONE-INFO_LEFT-12) / TEXT_ZOOM );

	m_textBody.LoadFromFont( sFont );
	m_textBody.SetHorizAlign( Actor::align_left );
	m_textBody.SetVertAlign( Actor::align_top );
	m_textBody.SetZoom( TEXT_ZOOM );

	m_textHint.SetMaxWidth( (PANEL_RIGHT-PANEL_LEFT-28) / 0.5f );

	m_Divider.SetDiffuse( RageColor(1,1,1,0.18f) );

	for( int i = 0; i < 2; ++i )
		m_Table[i].Load( sFont );

	FOREACH_PlayerNumber( p )
	{
		m_CounterBack[p].StretchTo( RectF(COUNTER_X[p]-40, COUNTER_Y-9, COUNTER_X[p]+40, COUNTER_Y+9) );
		m_CounterBack[p].SetDiffuse( RageColor(0,0,0,0.5f) );
		m_textCounter[p].LoadFromFont( sFont );
		m_textCounter[p].SetXY( COUNTER_X[p], COUNTER_Y );
		m_textCounter[p].SetZoom( 0.6f );
		m_textCounter[p].SetShadowLength( 0 );
	}
}


using ChartInfo::JudgeName;
using ChartInfo::PercentString;

/* The player whose chart the music select screen is showing. */
static PlayerNumber GetSelectPlayer()
{
	PlayerNumber pn = GAMESTATE->GetFirstHumanPlayer();
	if( pn == PLAYER_INVALID )
		pn = GAMESTATE->m_MasterPlayerNumber;
	return pn;
}

static float GetRate()
{
	return floorf( GAMESTATE->m_SongOptions.m_fMusicRate * 100 + 0.5f ) / 100;
}

static const float TABLE_HEAD_ZOOM = 0.6f, TABLE_SUB_ZOOM = 0.5f, TABLE_NOTE_ZOOM = 0.5f;
static const float TABLE_OVERALL_ZOOM = 0.75f, TABLE_ROW_ZOOM = 0.6f;
static const float TABLE_HEAD_HEIGHT = 17, TABLE_SUB_HEIGHT = 13;
static const float TABLE_OVERALL_HEIGHT = 21, TABLE_GAP = 7, TABLE_ROW_HEIGHT = 18;

void SkillTable::Load( const CString &sFont )
{
	m_bHasValues = false;
	m_bHasSub = false;

	m_textHead.LoadFromFont( sFont );
	m_textHead.SetHorizAlign( Actor::align_left );
	m_textHead.SetVertAlign( Actor::align_top );
	m_textHead.SetZoom( TABLE_HEAD_ZOOM );
	m_textHead.SetDiffuse( RageColor(0.75f,0.85f,1,1) );

	m_textSub.LoadFromFont( sFont );
	m_textSub.SetHorizAlign( Actor::align_left );
	m_textSub.SetVertAlign( Actor::align_top );
	m_textSub.SetZoom( TABLE_SUB_ZOOM );
	m_textSub.SetDiffuse( RageColor(0.7f,0.7f,0.7f,1) );

	m_textNote.LoadFromFont( sFont );
	m_textNote.SetHorizAlign( Actor::align_left );
	m_textNote.SetVertAlign( Actor::align_top );
	m_textNote.SetZoom( TABLE_NOTE_ZOOM );
	m_textNote.SetDiffuse( RageColor(0.85f,0.85f,0.85f,1) );

	for( int i = 0; i < MINACALC_NUM_SKILLSETS; ++i )
	{
		const float fZoom = i == MINACALC_OVERALL? TABLE_OVERALL_ZOOM: TABLE_ROW_ZOOM;
		m_textName[i].LoadFromFont( sFont );
		m_textName[i].SetHorizAlign( Actor::align_left );
		m_textName[i].SetVertAlign( Actor::align_top );
		m_textName[i].SetZoom( fZoom );
		m_textName[i].SetText( ChartMSD::SkillsetName(i) );
		m_textValue[i].LoadFromFont( sFont );
		m_textValue[i].SetHorizAlign( Actor::align_right );
		m_textValue[i].SetVertAlign( Actor::align_top );
		m_textValue[i].SetZoom( fZoom );
	}
}

void SkillTable::Set( const CString &sHead, const CString &sSub, const float *pValues, const CString &sNote, bool bNoteIsWarning )
{
	m_textHead.SetText( sHead );
	m_textSub.SetText( sSub );
	m_bHasSub = !sSub.empty();
	m_bHasValues = pValues != NULL;
	m_sNote = sNote;
	m_textNote.SetDiffuse( bNoteIsWarning? RageColor(1,0.7f,0.5f,1): RageColor(0.85f,0.85f,0.85f,1) );

	for( int i = 0; i < MINACALC_NUM_SKILLSETS && m_bHasValues; ++i )
	{
		m_textValue[i].SetText( ssprintf("%.2f", pValues[i]) );

		/* Overall is gold; the others are white, and a little darker if they're
		 * far under it, so the chart's strengths show. */
		RageColor c( 1, 1, 1, 1 );
		if( i == MINACALC_OVERALL )
			c = RageColor( 1, 0.9f, 0.6f, 1 );
		else if( pValues[MINACALC_OVERALL] > 0  &&  pValues[i] < pValues[MINACALC_OVERALL] * DIM_BELOW )
			c = RageColor( 0.6f, 0.6f, 0.6f, 1 );
		m_textName[i].SetDiffuse( c );
		m_textValue[i].SetDiffuse( c );
	}
}

float SkillTable::Layout( float fLeft, float fRight, float fTop )
{
	const float fWidth = fRight - fLeft;
	float y = fTop;

	m_textHead.SetXY( fLeft, y );
	m_textHead.SetMaxWidth( fWidth / TABLE_HEAD_ZOOM );
	y += TABLE_HEAD_HEIGHT;
	if( m_bHasSub )
	{
		m_textSub.SetXY( fLeft, y );
		m_textSub.SetMaxWidth( fWidth / TABLE_SUB_ZOOM );
		y += TABLE_SUB_HEIGHT;
	}

	if( m_bHasValues )
	{
		y += 2;
		for( int i = 0; i < MINACALC_NUM_SKILLSETS; ++i )
		{
			const float fZoom = i == MINACALC_OVERALL? TABLE_OVERALL_ZOOM: TABLE_ROW_ZOOM;
			m_textName[i].SetXY( fLeft, y );
			m_textValue[i].SetXY( fRight, y );
			/* The name gives way if the column is narrow. */
			m_textName[i].SetMaxWidth( (fWidth - 44) / fZoom );
			y += i == MINACALC_OVERALL? TABLE_OVERALL_HEIGHT + TABLE_GAP: TABLE_ROW_HEIGHT;
		}
		y += 2;
	}

	if( !m_sNote.empty() )
	{
		/* The note wraps inside the column. */
		m_textNote.SetText( m_sNote, "", (int) (fWidth / TABLE_NOTE_ZOOM) );
		const float fGap = m_bHasValues? 4: 2;
		m_textNote.SetXY( fLeft, y + fGap );
		y += fGap + m_textNote.GetZoomedHeight();
	}
	return y;
}

void SkillTable::Draw()
{
	m_textHead.Draw();
	if( m_bHasSub )
		m_textSub.Draw();
	if( m_bHasValues )
		for( int i = 0; i < MINACALC_NUM_SKILLSETS; ++i )
		{
			m_textName[i].Draw();
			m_textValue[i].Draw();
		}
	if( !m_sNote.empty() )
		m_textNote.Draw();
}

/* Puts the tables in their column, under row Y fTop. */
void WifeOverlay::LayoutTables( float fTop )
{
	m_fTablesBottom = fTop;
	for( int i = 0; i < m_iTables; ++i )
	{
		float fBottom;
		if( m_iTables == 1 )
			fBottom = m_Table[i].Layout( TABLE_LEFT_ONE, TABLE_RIGHT, fTop );
		else
			fBottom = m_Table[i].Layout( TABLE_LEFT_TWO[i], TABLE_RIGHT_TWO[i], fTop );
		m_fTablesBottom = max( m_fTablesBottom, fBottom );
	}
}

/* The left edge of the first table, or the panel's edge if there is none. */
static float TablesLeft( int iTables )
{
	if( iTables == 0 )
		return PANEL_RIGHT;
	return iTables == 1? TABLE_LEFT_ONE: TABLE_LEFT_TWO[0];
}

/* How wide what's left of the tables can be. */
static float TextWidth( int iTables )
{
	const float fRight = iTables == 0? PANEL_RIGHT-14: TablesLeft(iTables) - 12;
	return fRight - (PANEL_LEFT+14);
}

void WifeOverlay::SetPanel( const CString &sTitle, const CString &sSubtitle, const CString &sHint,
	const CString &sRanks, const CString &sWifes, const CString &sNames, const CString &sInfo,
	const CString &sMessage )
{
	m_bPanelIsBody = false;
	m_textTitle.SetText( sTitle );
	m_textSubtitle.SetText( sSubtitle );
	m_textHint.SetText( sHint );
	m_textRanks.SetText( sRanks );
	m_textWifes.SetText( sWifes );
	m_textNames.SetText( sNames );
	m_textInfo.SetText( sInfo );
	m_textMessage.SetText( sMessage, "", (int) (TextWidth(m_iTables) / TEXT_ZOOM) );

	m_fRowY = ROW_Y;
	m_textRanks.SetXY( RANK_RIGHT, m_fRowY );
	m_textWifes.SetXY( WIFE_RIGHT, m_fRowY );
	m_textNames.SetXY( NAME_LEFT, m_fRowY );
	m_textInfo.SetXY( INFO_LEFT, m_fRowY );
	m_textMessage.SetXY( PANEL_LEFT+14, m_fRowY );
	LayoutTables( m_fRowY );

	/* The panel is as tall as what's in it. */
	float fHeight = max( max(m_textRanks.GetZoomedHeight(), m_textNames.GetZoomedHeight()), m_textInfo.GetZoomedHeight() );
	if( sRanks.empty() && sNames.empty() )
		fHeight = 0;
	if( !sMessage.empty() )
		fHeight = max( fHeight, m_textMessage.GetZoomedHeight() );
	m_fPanelBottom = max( m_fRowY + fHeight, m_fTablesBottom ) + 34;
	m_PanelBack.StretchTo( RectF(PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, m_fPanelBottom) );
	m_textHint.SetXY( PANEL_LEFT+14, m_fPanelBottom-8 );
	m_Divider.StretchTo( RectF(TablesLeft(m_iTables)-6, m_fRowY, TablesLeft(m_iTables)-5, m_fPanelBottom-26) );
}

void WifeOverlay::SetBodyPanel( const CString &sTitle, const CString &sSubtitle, const CString &sHint, const CString &sBody )
{
	SetPanel( sTitle, sSubtitle, sHint, "", "", "", "" );
	m_bPanelIsBody = true;

	/* The text wraps inside the room left of the tables.  (The wrapping drops
	 * empty lines, so they get a space.) */
	CString sText = sBody;
	sText.Replace( "\n\n", "\n \n" );
	sText.Replace( "\n\n", "\n \n" );
	m_textBody.SetText( sText, "", (int) (TextWidth(m_iTables) / TEXT_ZOOM) );
	m_textBody.SetXY( PANEL_LEFT+14, m_fRowY );
	m_fPanelBottom = max( m_fRowY + m_textBody.GetZoomedHeight(), m_fTablesBottom ) + 34;
	m_PanelBack.StretchTo( RectF(PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, m_fPanelBottom) );
	m_textHint.SetXY( PANEL_LEFT+14, m_fPanelBottom-8 );
	m_Divider.StretchTo( RectF(TablesLeft(m_iTables)-6, m_fRowY, TablesLeft(m_iTables)-5, m_fPanelBottom-26) );
}

/* The chart's difficulty at the rate, or why there isn't one. */
void WifeOverlay::SetMSDTable( const Steps *pSteps, float fRate, bool bPending )
{
	const CString sHead = ssprintf( "Difficulty at %.2fx", fRate );
	m_iTables = 1;
	if( !ChartMSD::IsSupported(pSteps) )
	{
		m_Table[0].Set( sHead, "", NULL, "No rating.  Only 4-key dance-single charts are rated." );
		return;
	}
	if( bPending )
	{
		m_Table[0].Set( sHead, "", NULL, "..." );
		return;
	}
	float f[MINACALC_NUM_SKILLSETS];
	const bool bOK = ChartMSD::GetMSDAtRate( GAMESTATE->m_pCurSong, pSteps, fRate, f );
	if( !bOK  ||  f[MINACALC_OVERALL] <= 0 )
	{
		m_Table[0].Set( sHead, "", NULL, "No rating." );
		return;
	}
	m_Table[0].Set( sHead, "", f, "" );
}

/* A name that fits the names column, with "..." in place of what doesn't.  (A
 * BitmapText would shrink the whole column to fit its longest name.) */
CString WifeOverlay::ClipName( const CString &s )
{
	map<CString,CString>::const_iterator it = m_ClipCache.find( s );
	if( it != m_ClipCache.end() )
		return it->second;

	const float fMax = NAME_WIDTH;
	wstring w = CStringToWstring( s );
	CString sOut = s;
	m_textMeasure.SetText( s );
	if( m_textMeasure.GetZoomedWidth() > fMax )
	{
		while( !w.empty() )
		{
			w.erase( w.size()-1 );
			sOut = WStringToCString(w) + "...";
			m_textMeasure.SetText( sOut );
			if( m_textMeasure.GetZoomedWidth() <= fMax )
				break;
		}
	}
	if( m_ClipCache.size() > 500 )
		m_ClipCache.clear();
	m_ClipCache[s] = sOut;
	return sOut;
}

void WifeOverlay::UpdateSelectPanel( float fDeltaTime )
{
	const PlayerNumber pn = GetSelectPlayer();
	const Song *pSong = GAMESTATE->m_pCurSong;
	const Steps *pSteps = pn == PLAYER_INVALID? NULL: GAMESTATE->m_pCurSteps[pn];
	const float fRate = GetRate();

	/* The same panel as last time, as long as nothing it shows has changed. */
	m_fSettled += fDeltaTime;
	const bool bMSDDue = m_bMSDPending && m_fSettled >= MSD_SETTLE_SECONDS;
	if( pSteps == m_pSelectSteps  &&  fRate == m_fSelectRate  &&  m_bAllProfiles == m_bSelectAll  &&  m_bPanelVisible  &&  !bMSDDue )
		return;
	if( pSteps != m_pSelectSteps || fRate != m_fSelectRate )
	{
		m_fSettled = 0;
		m_bMSDPending = true;
	}
	m_pSelectSteps = pSteps;
	m_fSelectRate = fRate;
	m_bSelectAll = m_bAllProfiles;

	const CString sHint = "F5 hide      F6 this profile / all profiles";
	if( pSong == NULL || pSteps == NULL )
	{
		m_iTables = 0;
		SetPanel( "Wife3 leaderboard", "", sHint, "", "", "", "", "Pick a song to see its scores." );
		return;
	}

	const bool bPending = m_fSettled < MSD_SETTLE_SECONDS;
	if( !bPending )
		m_bMSDPending = false;
	SetMSDTable( pSteps, fRate, bPending );

	const CString sKey = WIFE->GetChartKey( pSong, pSteps );
	const CString sTitle = pSong->GetFullDisplayTitle() + "  " + DifficultyToThemedString( pSteps->GetDifficulty() ) + ssprintf( " %i", pSteps->GetMeter() );
	const CString sWho = m_bAllProfiles? CString("All profiles"):
		(PROFILEMAN->IsUsingProfile(pn)? PROFILEMAN->GetPlayerName(pn): CString("Machine"));
	const CString sSubtitle = ssprintf( "%.2fx   %s   %s", fRate, sWho.c_str(), sKey.c_str() );

	vector<WifeScoreEntry> v;
	WIFE->GetLeaderboard( pn, sKey, fRate, m_bAllProfiles, v );
	if( v.empty() )
	{
		SetPanel( sTitle, sSubtitle, sHint, "", "", "", "", "No scores on this chart at this rate yet." );
		return;
	}

	CString sRanks, sWifes, sNames, sInfo;
	for( unsigned i=0; i<v.size() && i<(unsigned) NUM_ROWS; i++ )
	{
		sRanks += ssprintf( "%i\n", i+1 );
		sWifes += PercentString( v[i].fWife ) + "\n";
		sNames += ClipName( v[i].sName ) + "\n";
		sInfo += ssprintf( "%s   %04i-%02i-%02i\n", JudgeName(v[i].fJudgeScale).c_str(),
			v[i].dateTime.tm_year+1900, v[i].dateTime.tm_mon+1, v[i].dateTime.tm_mday );
	}
	SetPanel( sTitle, sSubtitle, sHint, sRanks, sWifes, sNames, sInfo );
}

static CString Masked( const CString &s )
{
	return CString( s.size(), '*' );
}

void WifeOverlay::UpdateOnlinePanel()
{
	const PlayerNumber pn = m_OnlinePlayer;
	const CString sPlayer = ssprintf( "P%i", pn+1 );
	const CString sServer = ONLINE->GetServerName().empty()? ONLINE->GetServerURL(): ONLINE->GetServerName();
	CString sHint = "F7 next player / hide";

	m_iTables = 0;
	if( PREFSMAN->m_sOnlineServer.empty() )
	{
		SetBodyPanel( "Online scores", "Online is off", "F7 hide",
			"Online scores are off because OnlineServer is empty.\n\n"
			"To turn them on, set OnlineServer=https://aurasm.online in\n"
			"Data/StepMania.ini (that is the default) and restart the game." );
		return;
	}

	const CString sProblem = ONLINE->GetServerProblem();
	const OnlineAccount &a = ONLINE->GetAccount( pn );

	if( m_bFormOpen || !a.bLoggedIn )
	{
		CString sBody;
		if( !sProblem.empty() )
			sBody += sProblem + "\n\n";
		sBody += ssprintf( "Username:   %s%s\n", m_sFormUser.c_str(), (m_bFormOpen && !m_bFormOnPassword)? "_": "" );
		sBody += ssprintf( "Password:   %s%s\n\n", Masked(m_sFormPassword).c_str(), (m_bFormOpen && m_bFormOnPassword)? "_": "" );
		if( !a.sStatus.empty() )
			sBody += a.sStatus + "\n\n";
		sBody += "No account?  Register at " + ONLINE->GetRegisterURL();
		if( !ONLINE->GetClockWarning().empty() )
			sBody += "\n" + ONLINE->GetClockWarning();
		sHint = m_bFormOpen? "Tab next field      Enter log in      Esc close": "F7 log in";
		SetBodyPanel( "Log in: " + sPlayer, sServer, sHint, sBody );
		return;
	}

	ONLINE->RefreshAccount( pn );
	CString sWho = sPlayer + "  " + a.sUsername;
	if( a.bHasRating && a.iRank > 0 )
		sWho += ssprintf( "  #%i", a.iRank );
	m_iTables = 1;
	if( a.bHasRating )
		m_Table[0].Set( "Online rating", sWho, a.fRating, sProblem, true );
	else
		m_Table[0].Set( "Online rating", sWho, NULL, sProblem.empty()? CString("No rating yet."): sProblem, !sProblem.empty() );
	sHint = "F7 next player / hide      F6 log out";

	const Song *pSong = GAMESTATE->m_pCurSong;
	const Steps *pSteps = GAMESTATE->m_pCurSteps[pn];
	const float fRate = GetRate();
	if( pSong == NULL || pSteps == NULL )
	{
		SetPanel( "Online leaderboard", sServer, sHint, "", "", "", "", "Pick a song to see its scores." );
		return;
	}

	const CString sKey = WIFE->GetChartKey( pSong, pSteps );
	const CString sTitle = pSong->GetFullDisplayTitle() + "  " + DifficultyToThemedString( pSteps->GetDifficulty() ) + ssprintf( " %i", pSteps->GetMeter() );
	const CString sSubtitle = ssprintf( "%.2fx online   %s", fRate, sServer.c_str() );
	if( fRate < 0.7f || fRate > 2.0f )
	{
		/* The server ranks 0.7x to 2.0x only, so there is nothing to ask for. */
		SetPanel( sTitle, sSubtitle, sHint, "", "", "", "", "Only rates from 0.7x to 2.0x are ranked online." );
		return;
	}
	const OnlineLeaderboard &lb = ONLINE->GetLeaderboard( pn, sKey, fRate );

	if( !lb.sError.empty() )
	{
		SetPanel( sTitle, sSubtitle, sHint, "", "", "", "", lb.sError );
		return;
	}
	if( lb.vEntries.empty() )
	{
		SetPanel( sTitle, sSubtitle, sHint, "", "", "", "", lb.bLoading? "Loading...": "No scores on this chart at this rate yet." );
		return;
	}

	CString sRanks, sWifes, sNames, sInfo;
	bool bMeShown = false, bAnyPending = false;
	for( unsigned i = 0; i < lb.vEntries.size() && i < (unsigned) NUM_ROWS; ++i )
	{
		const OnlineLeaderboardEntry &e = lb.vEntries[i];
		sRanks += ssprintf( "%i\n", e.iRank );
		sWifes += PercentString( e.fWifeJ4 ) + "\n";
		sNames += ClipName( e.sUsername ) + "\n";
		sInfo += ssprintf( "%.2f  %s  %s%s\n", e.fSSROverall, JudgeName(e.fJudgeScale).c_str(), e.sDate.c_str(),
			e.sStatus == "pending"? " *": "" );
		if( e.sStatus == "pending" )
			bAnyPending = true;
		if( lb.bHasMe && e.iRank == lb.me.iRank && e.sUsername == lb.me.sUsername )
			bMeShown = true;
	}
	if( lb.bHasMe && !bMeShown )
	{
		const OnlineLeaderboardEntry &e = lb.me;
		sRanks += ssprintf( "\n%i\n", e.iRank );
		sWifes += "\n" + PercentString( e.fWifeJ4 ) + "\n";
		sNames += "\n" + ClipName( e.sUsername ) + "\n";
		sInfo += ssprintf( "\n%.2f  %s  %s\n", e.fSSROverall, JudgeName(e.fJudgeScale).c_str(), e.sDate.c_str() );
	}
	if( bAnyPending )
		sHint += "      * pending review";
	SetPanel( sTitle, sSubtitle + ssprintf("   %i player%s", lb.iTotal, lb.iTotal == 1? "": "s"), sHint, sRanks, sWifes, sNames, sInfo );
}

static CString OnlineStatusText( const OnlineStageResult &r )
{
	switch( r.state )
	{
	case OnlineStageResult::NOT_ONLINE:	return "";
	case OnlineStageResult::UNRANKED:	return "Online: unranked, " + r.sReason;
	case OnlineStageResult::NOT_SENT:	return "Online: not sent; F7 sends it";
	case OnlineStageResult::WAITING:	return "Online: waiting to send...";
	case OnlineStageResult::SENDING:	return "Online: sending...";
	case OnlineStageResult::QUEUED:		return "Online: queued, will try again (" + r.sReason + ")";
	case OnlineStageResult::REJECTED:	return "Online: rejected, " + r.sReason;
	case OnlineStageResult::ACCEPTED:
	case OnlineStageResult::PENDING:
		{
			CString s = r.state == OnlineStageResult::ACCEPTED? "Online: accepted": "Online: pending review";
			if( r.state == OnlineStageResult::PENDING && !r.sReason.empty() )
				s += " (" + r.sReason + ")";
			if( r.iRank > 0 )
				s += ssprintf( ", rank #%i", r.iRank );
			if( r.state == OnlineStageResult::ACCEPTED )
				s += ssprintf( ", rating %.2f -> %.2f", r.fRatingBefore, r.fRatingAfter );
			return s;
		}
	}
	return "";
}

void WifeOverlay::UpdateEvalPanel()
{
	CString sLines;
	bool bCanSend = false;

	/* The SSRs go in tables on the right; there are none unless some player
	 * has them. */
	float fSSR[NUM_PLAYERS][MINACALC_NUM_SKILLSETS];
	bool bHasSSR[NUM_PLAYERS];
	int iWithSSR = 0;
	FOREACH_HumanPlayer( pn )
	{
		const OnlineStageResult &o = ONLINE->GetStageResult( pn );
		bHasSSR[pn] = WIFE->GetStageResult(pn).bValid && o.bValid && o.bHasSSR && o.fSSR[MINACALC_OVERALL] > 0;
		if( bHasSSR[pn] )
		{
			memcpy( fSSR[pn], o.fSSR, sizeof(fSSR[pn]) );
			++iWithSSR;
		}
	}

	m_iTables = 0;
	FOREACH_HumanPlayer( pn )
	{
		if( iWithSSR == 0 )
			break;
		const WifeStageResult &r = WIFE->GetStageResult( pn );
		const OnlineStageResult &o = ONLINE->GetStageResult( pn );
		const CString sHead = ssprintf( "P%i SSR", pn+1 );
		if( bHasSSR[pn] )
			m_Table[m_iTables++].Set( sHead, ssprintf("at J4, %s", PercentString(o.fWifeJ4).c_str()), fSSR[pn], "" );
		else if( r.bValid )
			m_Table[m_iTables++].Set( sHead, "", NULL, "No SSR for this play." );
	}

	FOREACH_HumanPlayer( pn )
	{
		const WifeStageResult &r = WIFE->GetStageResult( pn );
		if( r.bValid )
		{
			sLines += ssprintf( "P%i   %s   (%s, %.2fx)\n", pn+1, PercentString(r.fWife).c_str(),
				JudgeName(r.fJudgeScale).c_str(), r.fRate );
			sLines += ssprintf( "Rank %i of %i on this profile\n", r.iRankProfile, r.iCountProfile );
			sLines += ssprintf( "Rank %i of %i on all profiles\n", r.iRankAll, r.iCountAll );
			const OnlineStageResult &o = ONLINE->GetStageResult( pn );
			const CString sOnline = OnlineStatusText( o );
			if( !sOnline.empty() )
				sLines += sOnline + "\n";
			if( o.state == OnlineStageResult::NOT_SENT )
				bCanSend = true;
			sLines += "\n";
		}
		else if( WIFE->HasScore(pn) )
			sLines += ssprintf( "P%i   %s   (not saved)\n\n", pn+1, PercentString(WIFE->GetFinalPercent(pn)).c_str() );
		else
			sLines += ssprintf( "P%i   no Wife3 score for this play\n\n", pn+1 );
	}
	while( !sLines.empty() && sLines[sLines.size()-1] == '\n' )
		sLines.erase( sLines.size()-1 );
	SetBodyPanel( "Wife3", "This play", bCanSend? "F5 hide      F7 send online": "F5 hide", sLines );
}

void WifeOverlay::UpdateCounters()
{
	const Screen *pTop = SCREENMAN->GetTopScreen();
	const bool bGameplay = dynamic_cast<const ScreenGameplay *>( pTop ) != NULL;
	FOREACH_PlayerNumber( p )
	{
		m_bCounterVisible[p] = bGameplay && PREFSMAN->m_bWife && PREFSMAN->m_bShowWifeInGameplay && WIFE->IsActive( p );
		if( m_bCounterVisible[p] )
			m_textCounter[p].SetText( PercentString( WIFE->GetLivePercent(p) ) );
	}
}

void WifeOverlay::Update( float fDeltaTime )
{
	if( !m_bInit )
		Init();

	Screen *pTop = SCREENMAN->GetTopScreen();
	ScreenSelectMusic *pSelect = dynamic_cast<ScreenSelectMusic *>( pTop );
	ScreenEvaluation *pEval = dynamic_cast<ScreenEvaluation *>( pTop );
	const bool bEval = pEval != NULL  &&  pEval->GetType() == ScreenEvaluation::stage;

	if( pSelect == NULL )
	{
		m_bShowSelect = false;
		m_bShowOnline = false;
		m_bFormOpen = false;
	}
	if( !bEval )
		m_bShowEval = false;
	if( !PREFSMAN->m_bWife )
		m_bShowSelect = m_bShowEval = false;
	/* With Wife on but no OnlineServer, F7 still opens the panel, to say how
	 * to turn online on. */
	if( !PREFSMAN->m_bWife )
		m_bShowOnline = false;
	if( !ONLINE->IsEnabled() )
		m_bFormOpen = false;
	if( !m_bFormOpen )
		m_sFormPassword = "";

	if( m_bShowOnline )
	{
		/* A successful login closes the form. */
		const OnlineAccount &a = ONLINE->GetAccount( m_OnlinePlayer );
		if( m_bFormOpen && a.bLoggedIn && !a.bBusy )
			m_bFormOpen = false;
		UpdateOnlinePanel();
		m_bPanelVisible = true;
		m_pSelectSteps = NULL;
	}
	else if( m_bShowSelect )
	{
		UpdateSelectPanel( fDeltaTime );
		m_bPanelVisible = true;
	}
	else if( m_bShowEval )
	{
		UpdateEvalPanel();
		m_bPanelVisible = true;
	}
	else
	{
		m_bPanelVisible = false;
		m_pSelectSteps = NULL;
	}

	UpdateCounters();
}

void WifeOverlay::ShowRate( float fRate, int iLimit )
{
	if( !m_bPopupInit )
	{
		m_bPopupInit = true;
		m_textPopup.LoadFromFont( THEME->GetPathToF( "Common normal" ) );
		m_textPopup.SetZoom( POPUP_ZOOM );
		m_textPopup.SetXY( CENTER_X, POPUP_Y );
		m_PopupBack.SetDiffuse( RageColor(0,0,0,0.75f) );
	}

	m_textPopup.SetText( ssprintf( "Rate %.2fx%s", fRate, iLimit < 0? " (min)": iLimit > 0? " (max)": "" ) );
	const float fHalfWidth = m_textPopup.GetZoomedWidth() / 2 + 14;
	m_PopupBack.StretchTo( RectF(CENTER_X-fHalfWidth, POPUP_Y-14, CENTER_X+fHalfWidth, POPUP_Y+14) );
	m_fPopupLeft = POPUP_SECONDS;
}

void WifeOverlay::UpdatePopup( float fDeltaTime )
{
	if( m_fPopupLeft > 0 )
		m_fPopupLeft = max( 0.0f, m_fPopupLeft - fDeltaTime );
}

void WifeOverlay::DrawPopup()
{
	if( !m_bPopupInit || m_fPopupLeft <= 0 )
		return;

	const float fAlpha = min( 1.0f, m_fPopupLeft / POPUP_FADE_SECONDS );
	m_PopupBack.SetDiffuse( RageColor(0,0,0,0.75f*fAlpha) );
	m_textPopup.SetDiffuse( RageColor(1,1,1,fAlpha) );
	m_PopupBack.Draw();
	m_textPopup.Draw();
}

void WifeOverlay::DrawPrimitives()
{
	if( !m_bInit )
	{
		DrawPopup();
		return;
	}

	if( m_bPanelVisible )
	{
		m_PanelBack.Draw();
		m_textTitle.Draw();
		m_textSubtitle.Draw();
		m_textHint.Draw();
		if( m_bPanelIsBody )
			m_textBody.Draw();
		else
		{
			m_textRanks.Draw();
			m_textWifes.Draw();
			m_textNames.Draw();
			m_textInfo.Draw();
			m_textMessage.Draw();
		}
		if( m_iTables > 0 )
			m_Divider.Draw();
		for( int i = 0; i < m_iTables; ++i )
			m_Table[i].Draw();
	}

	FOREACH_PlayerNumber( p )
		if( m_bCounterVisible[p] )
		{
			m_CounterBack[p].Draw();
			m_textCounter[p].Draw();
		}

	DrawPopup();
}

char TypedChar( const DeviceInput &DeviceI )
{
	char c = DeviceI.ToChar();
	if( c < 32 || c > 126 )
		return '\0';
	const bool bShift =
		INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, KEY_LSHIFT) ) ||
		INPUTFILTER->IsBeingPressed( DeviceInput(DEVICE_KEYBOARD, KEY_RSHIFT) );
	if( bShift )
	{
		static const char szFrom[] = "`1234567890-=[]\\;',./";
		static const char szTo[]   = "~!@#$%^&*()_+{}|:\"<>?";
		const char *p = strchr( szFrom, c );
		if( p != NULL )
			c = szTo[p - szFrom];
		else
			c = (char) toupper( c );
	}
	return c;
}

/* The login form takes the keyboard while it's open: printable characters
 * (with a US layout, as ScreenTextEntry does), Backspace, Tab, Enter, Esc.
 * Everything else is swallowed so it doesn't reach the screen below. */
bool WifeOverlay::FormInput( const DeviceInput &DeviceI, InputEventType type )
{
	if( DeviceI.device != DEVICE_KEYBOARD || type == IET_RELEASE || type == IET_LEVEL_CHANGED )
		return true;

	CString &sField = m_bFormOnPassword? m_sFormPassword: m_sFormUser;
	switch( DeviceI.button )
	{
	case KEY_ESC:
		if( type == IET_FIRST_PRESS )
		{
			m_bFormOpen = false;
			m_bShowOnline = false;
		}
		return true;
	case KEY_TAB:
		if( type == IET_FIRST_PRESS )
			m_bFormOnPassword = !m_bFormOnPassword;
		return true;
	case KEY_ENTER:
	case KEY_KP_ENTER:
		if( type != IET_FIRST_PRESS )
			return true;
		if( !m_bFormOnPassword )
			m_bFormOnPassword = true;
		else if( !m_sFormUser.empty() && !m_sFormPassword.empty() )
		{
			ONLINE->Login( m_OnlinePlayer, m_sFormUser, m_sFormPassword );
			m_sFormPassword = "";
		}
		return true;
	case KEY_BACK:
		if( !sField.empty() )
			sField.erase( sField.size()-1 );
		return true;
	}

	const char c = TypedChar( DeviceI );
	if( c != '\0' && sField.size() < 128 )
		sField += c;
	return true;
}

bool WifeOverlay::Input( const DeviceInput &DeviceI, InputEventType type )
{
	Screen *pTop = SCREENMAN->GetTopScreen();
	if( pTop == NULL )
		return false;

	ScreenSelectMusic *pSelect = dynamic_cast<ScreenSelectMusic *>( pTop );
	if( pSelect != NULL && m_bShowOnline && m_bFormOpen )
		return FormInput( DeviceI, type );

	if( type != IET_FIRST_PRESS  ||  DeviceI.device != DEVICE_KEYBOARD )
		return false;

	if( pSelect != NULL )
	{
		switch( DeviceI.button )
		{
		case KEY_F5:
			if( !PREFSMAN->m_bWife )
				return false;
			m_bShowSelect = !m_bShowSelect;
			m_bShowOnline = false;
			return true;
		case KEY_F6:
			if( m_bShowOnline )
			{
				ONLINE->Logout( m_OnlinePlayer );
				return true;
			}
			if( !m_bShowSelect )
				return false;
			m_bAllProfiles = !m_bAllProfiles;
			return true;
		case KEY_F7:
			{
				if( !PREFSMAN->m_bWife )
					return false;
				if( !ONLINE->IsEnabled() )
				{
					/* No OnlineServer: the panel tells how to set one. */
					m_bShowOnline = !m_bShowOnline;
					m_OnlinePlayer = GetSelectPlayer();
					m_bShowSelect = false;
					m_bFormOpen = false;
					return true;
				}
				/* Closed, then each human player in turn, then closed.  After
				 * logging out, F7 opens the form again first. */
				const PlayerNumber first = GetSelectPlayer();
				const bool bCanLogIn = PREFSMAN->m_bWife && ONLINE->IsBuiltIn();
				const bool bReopenForm = m_bShowOnline && bCanLogIn && !ONLINE->GetAccount(m_OnlinePlayer).bLoggedIn;
				if( !m_bShowOnline )
				{
					m_bShowOnline = true;
					m_OnlinePlayer = first;
				}
				else if( bReopenForm )
					;
				else if( m_OnlinePlayer == first && GAMESTATE->IsHumanPlayer(OPPOSITE_PLAYER[first]) )
					m_OnlinePlayer = OPPOSITE_PLAYER[first];
				else
					m_bShowOnline = false;
				m_bShowSelect = false;

				/* Not logged in: the form opens and takes the keyboard. */
				m_bFormOpen = false;
				if( m_bShowOnline && bCanLogIn && !ONLINE->GetAccount(m_OnlinePlayer).bLoggedIn )
				{
					m_bFormOpen = true;
					m_bFormOnPassword = false;
					m_sFormUser = ONLINE->GetAccount( m_OnlinePlayer ).sUsername;
					m_sFormPassword = "";
				}
				return true;
			}
		}
		return false;
	}

	ScreenEvaluation *pEval = dynamic_cast<ScreenEvaluation *>( pTop );
	if( pEval != NULL  &&  pEval->GetType() == ScreenEvaluation::stage )
	{
		if( DeviceI.button == KEY_F5 && PREFSMAN->m_bWife )
		{
			m_bShowEval = !m_bShowEval;
			return true;
		}
		if( DeviceI.button == KEY_F7 && ONLINE->IsEnabled() && PREFSMAN->m_bWife )
		{
			bool bSent = false;
			FOREACH_HumanPlayer( pn )
				if( ONLINE->GetStageResult(pn).state == OnlineStageResult::NOT_SENT )
				{
					ONLINE->SubmitStage( pn );
					bSent = true;
				}
			if( bSent )
				m_bShowEval = true;
			return bSent;
		}
	}

	return false;
}
