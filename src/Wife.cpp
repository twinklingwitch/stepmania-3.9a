/* Ported from Etterna (MIT license, see Wife.h). */
#include "global.h"
#include "Wife.h"

#include <math.h>

float Wife3Erf( float x )
{
	static const float a1 = 0.254829592f;
	static const float a2 = -0.284496736f;
	static const float a3 = 1.421413741f;
	static const float a4 = -1.453152027f;
	static const float a5 = 1.061405429f;
	static const float p = 0.3275911f;

	int sign = 1;
	if( x < 0.0f )
		sign = -1;
	x = fabsf( x );

	float t = 1.0f / (1.0f + p*x);
	float y = 1.0f - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1) * t * expf( -x*x );

	return sign * y;
}

float Wife3( float fOffsetSeconds, float fTimingScale )
{
	// so judge scaling isn't so extreme
	static const float j_pow = 0.75f;
	// offset at which points start decreasing (ms)
	float ridic = 5.0f * fTimingScale;

	// the boo window is 180ms at Judge 4; past it a tap is worth the miss weight
	float max_boo_weight = 180.0f * fTimingScale;

	// need positive values for this
	float maxms = fabsf( fOffsetSeconds * 1000.0f );

	if( maxms <= ridic )
		return WIFE3_MAX_POINTS;

	// piecewise inflection
	float zero = 65.0f * powf( fTimingScale, j_pow );
	float dev = 22.7f * powf( fTimingScale, j_pow );

	if( maxms <= zero )
		return WIFE3_MAX_POINTS * Wife3Erf( (zero - maxms) / dev );
	if( maxms <= max_boo_weight )
		return (maxms - zero) * WIFE3_MISS_WEIGHT / (max_boo_weight - zero);
	return WIFE3_MISS_WEIGHT;
}

float Wife3Truncate( float fFraction )
{
	return floorf( fFraction * 10000.0f ) / 100.0f;
}

void WifeTally::Reset( float fTotalPoints, float fTimingScale )
{
	m_fPoints = 0;
	m_fJudgedPoints = 0;
	m_fTotalPoints = fTotalPoints;
	m_fTimingScale = fTimingScale;
}

void WifeTally::Tap( float fOffsetSeconds )
{
	m_fPoints += Wife3( fOffsetSeconds, m_fTimingScale );
	m_fJudgedPoints += WIFE3_MAX_POINTS;
}

void WifeTally::Miss()
{
	m_fPoints += WIFE3_MISS_WEIGHT;
	m_fJudgedPoints += WIFE3_MAX_POINTS;
}

/* Hitting a mine and dropping a hold cost points but aren't notes of their own,
 * so they don't add to what's been judged. */
void WifeTally::Mine()
{
	m_fPoints += WIFE3_MINE_HIT_WEIGHT;
}

void WifeTally::HoldDrop()
{
	m_fPoints += WIFE3_HOLD_DROP_WEIGHT;
}

float WifeTally::GetLivePercent() const
{
	if( m_fJudgedPoints == 0 )
		return 0;
	return m_fPoints / m_fJudgedPoints;
}

float WifeTally::GetFinalPercent() const
{
	if( m_fTotalPoints <= 0 )
		return 0;
	return m_fPoints / m_fTotalPoints;
}
