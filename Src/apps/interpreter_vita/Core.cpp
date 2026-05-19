/*
 * Core.cpp — PS Vita entry point for the AGK interpreter (AGKPlayer).
 *
 * Unlike the C++ template, this runs AGK *bytecode*: at startup the
 * interpreter's app class looks for "bytecode.byc" in the read path
 * (app0:/ inside the VPK) and, if found, loads and executes it standalone —
 * no networking, no IDE. Drop any .agc game's compiled bytecode.byc + its
 * media/ folder into the VPK and it runs.
 *
 * Adapted from apps/template_vita/Core.cpp. Differences:
 *   - includes interpreter.h (the heavyweight `app` class / bytecode VM)
 *   - App.Loop() returns void
 *   - SetExtraAGKPlayerAssetsMode(2) — interpreter asset mode
 *   - START is NOT bound to quit (games use START); exit via the PS button.
 */

#include "agk.h"
#include "interpreter.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/appmgr.h>

#include <vitaGL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace AGK;

/* Set by the interpreter when the bytecode program finishes / errors. */
volatile int g_iVitaShouldQuit = 0;

void PlatformAppQuit() { g_iVitaShouldQuit = 1; }

/* Front touch panel is 1920x1088, screen 960x544 -> halve. */
static inline float touch_to_screen_x( int v ) { return (float)v * 0.5f; }
static inline float touch_to_screen_y( int v ) { return (float)v * 0.5f; }

#define AGK_TOUCH_ID_BASE  10
static int prev_touch_active[ SCE_TOUCH_MAX_REPORT ] = { 0 };

/* The Vita pad is exposed to games through AGK's raw joystick API
 * (GetRawJoystick*) — see cJoystick in VitaMisc.cpp. The engine polls it
 * itself every frame, so this only needs to forward the touch screen. */
static void poll_input()
{
    SceTouchData touch; memset(&touch, 0, sizeof(touch));
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    int active_this_frame[ SCE_TOUCH_MAX_REPORT ] = { 0 };

    for (int i = 0; i < touch.reportNum; ++i)
    {
        int id = AGK_TOUCH_ID_BASE + i;
        active_this_frame[i] = 1;
        float x = touch_to_screen_x( touch.report[i].x );
        float y = touch_to_screen_y( touch.report[i].y );

        if ( !prev_touch_active[i] )
        {
            agk::TouchPressed( id, (int)x, (int)y );
            if ( i == 0 ) { agk::MouseMove(0, (int)x, (int)y); agk::MouseLeftButton(0, 1); }
        }
        else
        {
            agk::TouchMoved( id, (int)x, (int)y );
            if ( i == 0 ) agk::MouseMove(0, (int)x, (int)y);
        }
    }

    for (int i = 0; i < SCE_TOUCH_MAX_REPORT; ++i)
    {
        if ( prev_touch_active[i] && !active_this_frame[i] )
        {
            int id = AGK_TOUCH_ID_BASE + i;
            agk::TouchReleased( id, 0, 0 );
            if ( i == 0 ) agk::MouseLeftButton(0, 0);
        }
        prev_touch_active[i] = active_this_frame[i];
    }
}

/* ----------------------------------------------------------------------
 * Boot splash — fallback indeterminate loading bar.
 *
 * Used only when no "splash.png" is bundled (see main()). Drawn with
 * glClear + glScissor only: no shaders, no immediate mode, so it is always
 * safe on the prebuilt vitaGL. It animates during the short warm-up; while
 * the (blocking) bytecode + asset load runs it stays frozen on screen — the
 * player never sees a black screen.
 * -------------------------------------------------------------------- */
static void splash_frame( float t )
{
    glDisable( GL_SCISSOR_TEST );
    glClearColor( 0.078f, 0.094f, 0.141f, 1.0f );   /* dark navy */
    glClear( GL_COLOR_BUFFER_BIT );

    const int trackW = 560, trackH = 18;
    const int trackX = (960 - trackW) / 2;
    const int trackY = (544 - trackH) / 2;

    glEnable( GL_SCISSOR_TEST );

    /* track */
    glScissor( trackX, trackY, trackW, trackH );
    glClearColor( 0.16f, 0.21f, 0.32f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    /* moving segment — ping-pongs across the track */
    const int segW = 150;
    float p = t - (int)t;                 /* 0..1 */
    if ( p > 0.5f ) p = 1.0f - p;          /* 0..0.5 */
    p *= 2.0f;                              /* 0..1 */
    int segX = trackX + (int)( p * (trackW - segW) );
    glScissor( segX, trackY, segW, trackH );
    glClearColor( 0.36f, 0.63f, 0.96f, 1.0f );
    glClear( GL_COLOR_BUFFER_BIT );

    glDisable( GL_SCISSOR_TEST );
    vglSwapBuffers( GL_FALSE );
}

/* Plain navy clear, no bar. Used for the very first frame, before
 * agk::InitGraphics(): there is no renderer yet, so this raw glClear is all
 * that is safe — it just guarantees the screen is never black. */
static void splash_clear( void )
{
    glDisable( GL_SCISSOR_TEST );
    glClearColor( 0.078f, 0.094f, 0.141f, 1.0f );   /* dark navy */
    glClear( GL_COLOR_BUFFER_BIT );
    vglSwapBuffers( GL_FALSE );
}

/* AGK resource IDs for the optional boot-splash image. Both are deleted
 * before App.Begin() runs the game bytecode, so the values cannot collide
 * with anything the game allocates. */
#define SPLASH_IMG_ID  9990
#define SPLASH_SPR_ID  9990

int main( int argc, char *argv[] )
{
    (void)argc; (void)argv;

    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(222);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK,  SCE_TOUCH_SAMPLING_STATE_START);

    vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

    /* Show something immediately so the screen is never black. The renderer
     * does not exist yet, so this is only a plain navy clear. */
    splash_clear();

    agk::SetCompanyName( "AGKVita" );
    /* Asset mode 2 = interpreter / AGK Player. */
    agk::SetExtraAGKPlayerAssetsMode( 2 );

    try
    {
        agk::InitGraphics( 0, AGK_RENDERER_MODE_PREFER_BEST, 0 );

        /* Boot splash. If "splash.png" is bundled (app0:/splash.png) it is
         * drawn full-screen through AGK's own renderer — the same GLES2
         * shader path the game itself uses, so it is exactly as safe as the
         * game. If the file is absent or fails to load, fall back to the
         * animated loading bar. Either way the image/bar stays frozen on
         * screen while the blocking load inside App.Begin() runs. */
        bool haveImage = false;
        if ( agk::GetFileExists( "splash.png" ) )
        {
            try
            {
                agk::SetVirtualResolution( 960, 544 );
                agk::SetClearColor( 20, 24, 36 );
                agk::LoadImage( SPLASH_IMG_ID, "splash.png" );
                agk::CreateSprite( SPLASH_SPR_ID, SPLASH_IMG_ID );
                agk::SetSpriteSize( SPLASH_SPR_ID, 960, 544 );
                agk::SetSpritePosition( SPLASH_SPR_ID, 0, 0 );
                agk::Sync();   /* render once — the framebuffer holds it frozen */
                agk::DeleteSprite( SPLASH_SPR_ID );
                agk::DeleteImage( SPLASH_IMG_ID );
                haveImage = true;
            }
            catch( ... )
            {
                haveImage = false;   /* corrupt PNG etc. -> use the bar */
            }
        }

        if ( !haveImage )
        {
            /* Animated loading-bar warm-up — sweeps across ~1 s. */
            for ( int f = 0; f < 60; f++ ) splash_frame( f / 40.0f );
        }

        App.Begin();

        if ( !haveImage )
            splash_frame( 1.0f );   /* keep the bar up during first-frame load */
    }
    catch( ... )
    {
        uString err = agk::GetLastError();
        printf("Uncaught exception in Begin: %s\n", err.GetStr());
        g_iVitaShouldQuit = 1;
    }

    while ( !g_iVitaShouldQuit )
    {
        poll_input();

        try
        {
            App.Loop();
        }
        catch( ... )
        {
            uString err = agk::GetLastError();
            printf("Uncaught exception in Loop: %s\n", err.GetStr());
            g_iVitaShouldQuit = 1;
        }
    }

    App.End();
    agk::CleanUp();

    sceKernelExitProcess(0);
    return 0;
}
