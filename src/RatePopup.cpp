#include "global.h"
#include "RatePopup.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "ThemeManager.h"

static const float POPUP_SECONDS = 1.5f;
static const float POPUP_FADE_SECONDS = 0.3f;
static const float POPUP_Y = 36;
static const float POPUP_ZOOM = 0.9f;

RatePopup::RatePopup()
{
	m_bInit = false;
	m_fLeft = 0;
}

void RatePopup::Show( float fRate, int iLimit )
{
	if( !m_bInit )
	{
		m_bInit = true;
		m_text.LoadFromFont( THEME->GetPathToF( "Common normal" ) );
		m_text.SetZoom( POPUP_ZOOM );
		m_text.SetXY( CENTER_X, POPUP_Y );
		m_Back.SetDiffuse( RageColor(0,0,0,0.75f) );
	}

	m_text.SetText( ssprintf( "Rate %.2fx%s", fRate, iLimit < 0? " (min)": iLimit > 0? " (max)": "" ) );
	const float fHalfWidth = m_text.GetZoomedWidth() / 2 + 14;
	m_Back.StretchTo( RectF(CENTER_X-fHalfWidth, POPUP_Y-14, CENTER_X+fHalfWidth, POPUP_Y+14) );
	m_fLeft = POPUP_SECONDS;
}

void RatePopup::Update( float fDeltaTime )
{
	if( m_fLeft > 0 )
		m_fLeft = max( 0.0f, m_fLeft - fDeltaTime );
}

void RatePopup::Draw()
{
	if( !m_bInit || m_fLeft <= 0 )
		return;

	const float fAlpha = min( 1.0f, m_fLeft / POPUP_FADE_SECONDS );
	m_Back.SetDiffuse( RageColor(0,0,0,0.75f*fAlpha) );
	m_text.SetDiffuse( RageColor(1,1,1,fAlpha) );
	m_Back.Draw();
	m_text.Draw();
}
