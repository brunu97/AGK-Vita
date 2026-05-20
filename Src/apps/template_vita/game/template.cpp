/*
 * AGK Vita Tier 2 demo — toolchain validator.
 *
 * Self-contained (no external asset required): builds an in-memory coloured
 * sprite, moves it around the screen with the pad (left analog stick and
 * d-pad), flashes it on the Cross button, and prints FPS top-left.
 *
 * If this runs at 60 fps on the Vita, the full Tier 2 toolchain
 * (engine + renderer + input + text + Begin/Loop/End lifecycle) is wired
 * correctly. Replace this file with the real game once the demo passes.
 */
#include "template.h"

using namespace AGK;

app App;

#define TXT_FPS    1
#define TXT_HINT   2
#define IMG_BLOCK  1
#define SPR_BLOCK  1

void app::Begin( void )
{
    agk::SetWindowTitle( "AGK Vita Tier 2 Demo" );
    agk::SetVirtualResolution( 960, 544 );
    agk::SetClearColor( 30, 30, 60 );
    agk::SetSyncRate( 60, 1 );
    agk::UseNewDefaultFonts( 1 );

    /* FPS counter (top-left) */
    agk::CreateText( TXT_FPS, "FPS: --" );
    agk::SetTextSize( TXT_FPS, 24 );
    agk::SetTextColor( TXT_FPS, 200, 220, 255, 255 );
    agk::SetTextPosition( TXT_FPS, 8, 8 );

    /* Hint (bottom-centre) */
    agk::CreateText( TXT_HINT, "D-Pad / left stick to move  -  Cross to flash" );
    agk::SetTextSize( TXT_HINT, 22 );
    agk::SetTextColor( TXT_HINT, 180, 180, 200, 255 );
    agk::SetTextAlignment( TXT_HINT, 1 );
    agk::SetTextPosition( TXT_HINT, 480, 510 );

    /* In-memory coloured block — no external image required. */
    agk::CreateImageColor( IMG_BLOCK, 80, 200, 255, 255 );
    agk::CreateSprite( SPR_BLOCK, IMG_BLOCK );
    agk::SetSpriteSize( SPR_BLOCK, 80, 80 );
    agk::SetSpritePositionByOffset( SPR_BLOCK, 480, 272 );
}

int app::Loop( void )
{
    /* Read the pad — analog stick first, then d-pad if pressed. */
    float dx = agk::GetRawJoystickX( 1 );
    float dy = agk::GetRawJoystickY( 1 );
    int   pov = agk::GetRawJoystickPOV( 1, 1 );
    if ( pov >= 0 )
    {
        if      ( pov >= 315 || pov <= 45 )   dy = -1.0f;
        else if ( pov >= 135 && pov <= 225 )  dy =  1.0f;
        if      ( pov >= 45  && pov <= 135 )  dx =  1.0f;
        else if ( pov >= 225 && pov <= 315 )  dx = -1.0f;
    }

    const float speed = 5.0f;
    float x = agk::GetSpriteX( SPR_BLOCK ) + dx * speed;
    float y = agk::GetSpriteY( SPR_BLOCK ) + dy * speed;
    if ( x < 0 ) x = 0; else if ( x > 880 ) x = 880;
    if ( y < 0 ) y = 0; else if ( y > 464 ) y = 464;
    agk::SetSpritePosition( SPR_BLOCK, x, y );

    /* Cross (raw joystick button 1) flashes the block yellow. */
    if ( agk::GetRawJoystickButtonState( 1, 1 ) )
        agk::SetSpriteColor( SPR_BLOCK, 255, 240, 100, 255 );
    else
        agk::SetSpriteColor( SPR_BLOCK, 80, 200, 255, 255 );

    /* Live FPS. */
    uString s;
    s.Format( "FPS: %d", (int)agk::ScreenFPS() );
    agk::SetTextString( TXT_FPS, s.GetStr() );

    agk::Sync();
    return 0;   /* return 1 to quit (the PS button also exits cleanly). */
}

void app::End( void )
{
}
