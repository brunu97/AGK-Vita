/*
 * Core.cpp — PS Vita entry point for an AGK-based application.
 *
 * Responsibilities:
 *   1. Initialise the Sce subsystems (power, ctrl, touch).
 *   2. Initialise vitaGL — this creates the SceGxm context behind the OpenGL
 *      ES 2 facade that the AGK OpenGLES2 renderer relies on.
 *   3. Hand the engine an opaque "renderer pointer" via PlatformInitGraphics
 *      and run the canonical AGK Begin/Loop/End cycle.
 *   4. Translate Sce input events into AGK input events every frame.
 *   5. Tear everything down on exit (PS button + Close App, START on the
 *      template, or App.Loop() returning 1).
 */

#include "agk.h"
#include "template.h"

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

/* Signals to the main loop that the user (or the system) wants to exit. */
volatile int g_iVitaShouldQuit = 0;

void PlatformAppQuit() { g_iVitaShouldQuit = 1; }

/* Touch panel native resolution -> screen pixels conversion. The front panel
 * reports 1920x1088, screen is 960x544 -> divide by 2. */
static inline float touch_to_screen_x( int v ) { return (float)v * 0.5f; }
static inline float touch_to_screen_y( int v ) { return (float)v * 0.5f; }

/* AGK button id allocation per touch slot. Touch IDs must be stable across
 * frames so the engine can track drag/release; we pin them to slot index. */
#define AGK_TOUCH_ID_BASE  10

static int prev_touch_active[ SCE_TOUCH_MAX_REPORT ] = { 0 };
static int prev_buttons = 0;

/* Map a SceCtrl button bit to the AGK virtual key code used by the runtime
 * keyboard layer. The engine treats anything in [0..255] as a keyboard key. */
static void emit_button_edges( int now, int was, int mask, int agk_key )
{
    int now_down = (now & mask) ? 1 : 0;
    int was_down = (was & mask) ? 1 : 0;
    if (  now_down && !was_down ) agk::KeyDown( agk_key );
    if ( !now_down &&  was_down ) agk::KeyUp  ( agk_key );
}

static void poll_input()
{
    SceCtrlData pad; memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);

    /* Map face / dpad / shoulder to keyboard-style keys. Keep these mappings
     * obvious: AGK exposes both "key codes" (PC-style ints) and the modern
     * GetVirtualButtonState API — the basic-language Print(ScreenFPS) example
     * doesn't need either, but a real game will read these. */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_CROSS,    13);  /* Enter   */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_CIRCLE,   27);  /* Escape  */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_SQUARE,   88);  /* X       */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_TRIANGLE, 89);  /* Y       */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_UP,       38);
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_DOWN,     40);
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_LEFT,     37);
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_RIGHT,    39);
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_LTRIGGER, 81);  /* Q */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_RTRIGGER, 87);  /* W */
    emit_button_edges(pad.buttons, prev_buttons, SCE_CTRL_SELECT,   9);   /* Tab */

    /* START always quits the template — matches the standalone MVP. */
    if ( (pad.buttons & SCE_CTRL_START) && !(prev_buttons & SCE_CTRL_START) )
        g_iVitaShouldQuit = 1;

    prev_buttons = pad.buttons;

    /* ---- Touch input. Each active finger becomes a touch event with a
     * stable id derived from the slot index. The engine's pointer/mouse
     * abstraction is driven from the first touch as well. */
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

int main( int argc, char *argv[] )
{
    (void)argc; (void)argv;

    /* Bump clocks. Without this the GPU runs at 111 MHz which is unusable. */
    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(222);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK,  SCE_TOUCH_SAMPLING_STATE_START);

    /* vitaGL initialisation. The GPU memory pool is in bytes — 8 MiB is the
     * smallest sane value, AGK 3D apps will want 32+ MiB. */
    vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

    /* Tell AGK who we are *before* PlatformInitFilePaths so the write path
     * folder under ux0:data/ comes out correctly. */
    agk::SetCompanyName( COMPANY_NAME );
    agk::SetExtraAGKPlayerAssetsMode( 1 );

    try
    {
        /* InitGraphics creates the renderer + GL context. flags = 0 picks
         * the engine defaults (no MSAA, no debug). */
        agk::InitGraphics( 0, AGK_RENDERER_MODE_PREFER_BEST, 0 );
        App.Begin();
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
            if ( App.Loop() == 1 ) g_iVitaShouldQuit = 1;
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

    /* Modern vitaGL has no vglEnd(); sceKernelExitProcess does the teardown. */
    sceKernelExitProcess(0);
    return 0;
}
