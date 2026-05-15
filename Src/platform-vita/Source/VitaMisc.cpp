/*
 * VitaMisc.cpp — small platform-specific pieces for the AGK Vita port.
 *
 * Groups the leftover platform symbols that don't warrant their own file:
 *   - cEditBox  : on-screen text entry (handled via SceIme in VitaCore;
 *                 these per-box hooks are no-ops for now)
 *   - cJoystick : the Vita's built-in pad is exposed as raw joystick #1 so
 *                 games can use the standard GetRawJoystick* API
 *   - cImage    : camera capture / debug print — no camera on Vita
 */

#include "agk.h"

#include <psp2/ctrl.h>

namespace AGK
{

/* ---- cEditBox — on-screen keyboard hooks -------------------------- */
void cEditBox::PlatformStartText()      {}
void cEditBox::PlatformUpdateExternal() {}
bool cEditBox::PlatformUpdateText()     { return false; }
void cEditBox::PlatformUpdateTextEnd()  {}
void cEditBox::PlatformEndText()        {}

/* ---- cJoystick — Vita built-in pad as raw joystick #1 -------------
 *
 * The Vita's own controls are presented to AGK as a single physical
 * joystick at index 1. Button order matches a standard gamepad so that
 * games written for Xbox/PlayStation pads work unchanged:
 *
 *   AGK button 1 (A)  -> Cross        AGK button 6 (RB)    -> R
 *   AGK button 2 (B)  -> Circle       AGK button 7 (Back)  -> Select
 *   AGK button 3 (X)  -> Square       AGK button 8 (Start) -> Start
 *   AGK button 4 (Y)  -> Triangle     AGK button 9/10      -> (no L3/R3)
 *   AGK button 5 (LB) -> L
 *
 * The d-pad is reported through POV hat 0 (angle in degrees, -1 centred),
 * the left stick through the X/Y axes and the right stick through RX/RY.
 * The Vita has no analogue triggers, so the Z axis stays at 0.
 */
void cJoystick::DetectJoysticks()
{
	/* Create joystick #1 once; it never disconnects on a handheld. */
	if ( !agk::m_pJoystick[0] )
	{
		cJoystick *pJoy = new cJoystick( 0 );
		pJoy->SetName( "PS Vita" );
		pJoy->m_iConnected = 1;
		pJoy->m_iNumButtons = 10;
		agk::m_pJoystick[0] = pJoy;
	}
}

void cJoystick::PlatformUpdate()
{
	SceCtrlData pad;
	memset( &pad, 0, sizeof(pad) );
	sceCtrlPeekBufferPositive( 0, &pad, 1 );

	unsigned int b = pad.buttons;

	/* Face / shoulder / system buttons (m_iButtons is 0-indexed; AGK's
	 * raw joystick API exposes them 1-indexed). */
	m_iButtons[0] = ( b & SCE_CTRL_CROSS )    ? 1 : 0;   /* A     */
	m_iButtons[1] = ( b & SCE_CTRL_CIRCLE )   ? 1 : 0;   /* B     */
	m_iButtons[2] = ( b & SCE_CTRL_SQUARE )   ? 1 : 0;   /* X     */
	m_iButtons[3] = ( b & SCE_CTRL_TRIANGLE ) ? 1 : 0;   /* Y     */
	m_iButtons[4] = ( b & SCE_CTRL_LTRIGGER ) ? 1 : 0;   /* LB (L)*/
	m_iButtons[5] = ( b & SCE_CTRL_RTRIGGER ) ? 1 : 0;   /* RB (R)*/
	m_iButtons[6] = ( b & SCE_CTRL_SELECT )   ? 1 : 0;   /* Back  */
	m_iButtons[7] = ( b & SCE_CTRL_START )    ? 1 : 0;   /* Start */
	m_iButtons[8] = 0;                                   /* no L3 */
	m_iButtons[9] = 0;                                   /* no R3 */

	/* D-pad -> POV hat, angle in degrees (0 = up), -1 when centred. */
	int up    = ( b & SCE_CTRL_UP )    ? 1 : 0;
	int down  = ( b & SCE_CTRL_DOWN )  ? 1 : 0;
	int left  = ( b & SCE_CTRL_LEFT )  ? 1 : 0;
	int right = ( b & SCE_CTRL_RIGHT ) ? 1 : 0;

	int pov = -1;
	if      ( up    && right ) pov = 45;
	else if ( right && down  ) pov = 135;
	else if ( down  && left  ) pov = 225;
	else if ( left  && up    ) pov = 315;
	else if ( up    )          pov = 0;
	else if ( right )          pov = 90;
	else if ( down  )          pov = 180;
	else if ( left  )          pov = 270;

	/* The engine stores up to 4 hats; fill 0 and 1 since games differ
	 * on which index they query. */
	m_iPOV[0] = pov;
	m_iPOV[1] = pov;

	/* Analogue sticks: SceCtrl reports 0..255 with 128 centred. */
	m_fX  = ( (float)pad.lx - 128.0f ) / 128.0f;   /* left  stick X */
	m_fY  = ( (float)pad.ly - 128.0f ) / 128.0f;   /* left  stick Y (down = +) */
	m_fRX = ( (float)pad.rx - 128.0f ) / 128.0f;   /* right stick X */
	m_fRY = ( (float)pad.ry - 128.0f ) / 128.0f;   /* right stick Y */

	/* No analogue triggers on the Vita. */
	m_fZ  = 0.0f;
	m_fRZ = 0.0f;
}

/* ---- cImage ------------------------------------------------------- */
bool cImage::CaptureFromCamera()        { return false; }
void cImage::Print( float /*size*/ )    {}

}
