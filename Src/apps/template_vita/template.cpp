#include "template.h"

using namespace AGK;

app App;

/*
 * 2D + audio + file-loading test.
 *
 *   - LoadImage("logo.png")    : load a PNG from a bundled file
 *   - CreateImageColor         : an in-memory texture
 *   - CreateSprite             : textured + plain coloured sprites
 *   - LoadSound("beep.wav")    : load a WAV
 *   - PlaySound on X press     : exercises the sceAudioOut mixer
 *
 * Press X (Cross) to hear a beep. A loaded-from-file logo sits top-left,
 * a generated box bounces and rotates, three coloured squares sit below.
 */

#define IMG_WHITE   1
#define IMG_LOGO    2
#define SPR_TEX     1
#define SPR_LOGO    2
#define SPR_RED     3
#define SPR_GREEN   4
#define SPR_BLUE    5
#define SND_BEEP    1
#define MUS_TUNE    1

/* SCE_CTRL_CROSS is mapped to key code 13 by Core.cpp's poll_input(). */
#define KEY_CROSS   13

static float g_x = 100.0f, g_y = 100.0f;
static float g_vx = 180.0f, g_vy = 140.0f;
static float g_angle = 0.0f;

void app::Begin( void )
{
    agk::SetVirtualResolution( 960, 544 );
    agk::SetClearColor( 20, 24, 36 );
    agk::SetSyncRate( 60, 0 );
    agk::SetScissor( 0, 0, 0, 0 );
    agk::UseNewDefaultFonts( 1 );

    /* In-memory texture for the bouncing sprite. */
    agk::CreateImageColor( IMG_WHITE, 255, 255, 255, 255 );
    agk::CreateSprite( SPR_TEX, IMG_WHITE );
    agk::SetSpriteSize( SPR_TEX, 96, 96 );
    agk::SetSpriteColor( SPR_TEX, 255, 200, 80, 255 );

    /* PNG loaded from a file bundled in the VPK (app0:/logo.png). */
    agk::LoadImage( IMG_LOGO, "logo.png" );
    agk::CreateSprite( SPR_LOGO, IMG_LOGO );
    agk::SetSpriteSize( SPR_LOGO, 160, 160 );
    agk::SetSpritePosition( SPR_LOGO, 30, 30 );

    /* Plain coloured sprites. */
    agk::CreateSprite( SPR_RED, 0 );
    agk::SetSpriteSize( SPR_RED, 110, 110 );
    agk::SetSpritePosition( SPR_RED, 200, 400 );
    agk::SetSpriteColor( SPR_RED, 220, 60, 60, 255 );

    agk::CreateSprite( SPR_GREEN, 0 );
    agk::SetSpriteSize( SPR_GREEN, 110, 110 );
    agk::SetSpritePosition( SPR_GREEN, 420, 400 );
    agk::SetSpriteColor( SPR_GREEN, 60, 200, 90, 255 );

    agk::CreateSprite( SPR_BLUE, 0 );
    agk::SetSpriteSize( SPR_BLUE, 110, 110 );
    agk::SetSpritePosition( SPR_BLUE, 640, 400 );
    agk::SetSpriteColor( SPR_BLUE, 70, 120, 230, 255 );

    /* WAV loaded from a bundled file. */
    agk::LoadSound( SND_BEEP, "beep.wav" );

    /* OGG music — starts looping immediately. */
    agk::LoadMusicOGG( MUS_TUNE, "music.ogg" );
    agk::SetMusicVolumeOGG( MUS_TUNE, 80 );
    agk::PlayMusicOGG( MUS_TUNE, 1 );   /* 1 = loop forever */
}

int app::Loop( void )
{
    float dt = agk::GetFrameTime();

    g_x += g_vx * dt;
    g_y += g_vy * dt;
    if ( g_x < 0 )        { g_x = 0;        g_vx = -g_vx; }
    if ( g_x > 960 - 96 ) { g_x = 960 - 96; g_vx = -g_vx; }
    if ( g_y < 0 )        { g_y = 0;        g_vy = -g_vy; }
    if ( g_y > 544 - 96 ) { g_y = 544 - 96; g_vy = -g_vy; }

    g_angle += 90.0f * dt;
    if ( g_angle >= 360.0f ) g_angle -= 360.0f;

    agk::SetSpritePosition( SPR_TEX, g_x, g_y );
    agk::SetSpriteAngle( SPR_TEX, g_angle );

    /* X plays the beep — verifies the sceAudioOut software mixer. */
    if ( agk::GetRawKeyPressed( KEY_CROSS ) )
        agk::PlaySound( SND_BEEP );

    agk::Print( "Music looping - press X for a beep" );
    agk::Print( agk::ScreenFPS() );
    agk::Sync();

    return 0;
}

void app::End( void )
{
}
