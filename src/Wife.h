/* Wife - Etterna-style Wife3 scoring.  This is an optional addition to 3.9: it
 * is only used when the Wife preference is on, and it never touches the score,
 * grade or life that 3.9 keeps.
 *
 * The scoring curve, the erf approximation and the miss/mine/hold-drop weights
 * are ported from Etterna (src/RageUtil/Utils/RageUtil.h and Player.cpp),
 * which is MIT licensed: Copyright (c) 2016-2023 Etterna <etternadev@gmail.com>.
 * The license text is at the bottom of this file. */

#ifndef WIFE_H
#define WIFE_H

/* What each kind of mistake is worth.  A perfect tap is worth WIFE3_MAX_POINTS. */
const float WIFE3_MAX_POINTS = 2.0f;
const float WIFE3_MINE_HIT_WEIGHT = -7.0f;
const float WIFE3_HOLD_DROP_WEIGHT = -4.5f;
const float WIFE3_MISS_WEIGHT = -5.5f;

/* erf approximation, Abramowitz & Stegun formula 7.1.26. */
float Wife3Erf( float x );

/* Points for a tap hit fOffsetSeconds away from the note.  fTimingScale is the
 * judge window scale: 1.0 is Judge 4, which is also 3.9's default. */
float Wife3( float fOffsetSeconds, float fTimingScale );

/* Keeps the running Wife3 total for one player for one song.  Percentages are
 * fractions (0.9876 is 98.76%), like 3.9's own percent scores. */
class WifeTally
{
public:
	WifeTally() { Reset( 0, 1 ); }

	/* fTotalPoints is the most the whole chart can give: 2 per tap and hold head. */
	void Reset( float fTotalPoints, float fTimingScale );

	void Tap( float fOffsetSeconds );
	void Miss();
	void Mine();
	void HoldDrop();

	/* Points over the notes judged so far.  This is what a live counter shows. */
	float GetLivePercent() const;
	/* Points over everything in the chart: what's saved with the score. */
	float GetFinalPercent() const;

	float GetPoints() const { return m_fPoints; }
	float GetTotalPoints() const { return m_fTotalPoints; }
	float GetTimingScale() const { return m_fTimingScale; }

private:
	float m_fPoints;
	float m_fJudgedPoints;	// max points of the notes judged so far
	float m_fTotalPoints;
	float m_fTimingScale;
};

/* A fraction as a percentage truncated to two decimals, like Etterna shows it,
 * so 0.99996 doesn't turn into 100.00. */
float Wife3Truncate( float fFraction );

#endif

/*
 * Etterna's license, which covers the ported parts of this file:
 *
 * Copyright (c) 2016-2023 Etterna <etternadev@gmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
