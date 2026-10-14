/* RatePopup - "Rate 1.20x" for a moment at the top of the screen, when the
 * music select screen's rate keys change the music rate.  Drawn over
 * everything by the system layer, with a font every theme falls back to. */

#ifndef RATE_POPUP_H
#define RATE_POPUP_H

#include "BitmapText.h"
#include "Quad.h"

class RatePopup
{
public:
	RatePopup();

	/* iLimit is -1 or +1 if the rate is at its lowest or highest, to say so. */
	void Show( float fRate, int iLimit );
	void Update( float fDeltaTime );
	void Draw();

private:
	bool m_bInit;
	float m_fLeft;		// seconds it stays up, 0 if it isn't
	Quad m_Back;
	BitmapText m_text;
};

#endif
