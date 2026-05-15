/*
 * VitaCore.cpp — PS Vita platform layer for AppGameKit.
 *
 * This file owns the 297 agk::* methods that PiCore.cpp / LinuxCore.cpp
 * implement on POSIX. The structure mirrors PiCore.cpp; for each function we
 * either:
 *   (a) provide a real Vita-native implementation (file paths, time, input,
 *       GL init via vitaGL, threading, swap, etc.), or
 *   (b) return a zero/empty stub for features the Vita has no equivalent for
 *       (AdMob, Chartboost, GameCenter, ARKit, IAP, push notifications,
 *       Firebase, video playback, smartwatch, NFC, GPS, …).
 *
 * The boundaries are explicit: every stub is grouped under "// === STUB ==="
 * comments so it's obvious what is still TODO vs. what is deliberately empty.
 *
 * Filesystem convention:
 *   read    -> app0:/         (bundled with the VPK, read-only)
 *   write   -> ux0:data/<TITLEID>/   (per-app persistent storage)
 *
 * NOTE: this is the initial scaffold. The body of many functions matches the
 * PiCore equivalent intentionally — once the engine builds end-to-end on the
 * Vita, we can iterate on the parts that need true Vita-specific behaviour.
 */

#include "agk.h"
#include "OpenGL_ES2.h"
#include "VitaNetwork.h"
#include "VitaThread.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
#include <psp2/appmgr.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include <vitaGL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>

namespace AGK
{
    extern Renderer *g_pRenderer;
}

using namespace AGK;

/* ===================================================================
 * File path globals — same names PiCore.cpp uses internally.
 * =================================================================== */
static char szRootDir         [ MAX_PATH ] = "app0:/";
static char szBaseDir         [ MAX_PATH ] = "app0:/";
static char szWriteDir        [ MAX_PATH ] = "ux0:data/AGKVITA/";
static char szOriginalWriteDir[ MAX_PATH ] = "ux0:data/AGKVITA/";
static char szAppFolderName   [ MAX_PATH ] = "AGKVITA";

/* Time anchors used by PlatformUpdateTime — matches PiCore globals. */
static unsigned int uFixTime  = 0;
static double       fFixTime2 = 0.0;

/* Text input scratchpad — same approach as PiCore. */
static float g_fTextStartX = 0.0f;
static float g_fTextStartY = 0.0f;
static cSprite *g_pTextInputCursor = 0;
static cSprite *g_pTextInputArea   = 0;
static cSprite *g_pTextInputBack   = 0;
static cText   *g_pTextInputText   = 0;
static cSprite *pTextBackground    = 0;

int agk::m_iKeyboardMode = 0; // Vita has no hardware keyboard — IME is on-screen

extern volatile int g_iVitaShouldQuit; // set from main loop (Core.cpp)

/* ===================================================================
 * Platform glue that lives outside the agk:: namespace
 * =================================================================== */

/* AGKfopen is declared `extern "C"` in Common.h — must match that linkage. */
extern "C" FILE* AGKfopen( const char *szPath, const char *mode )
{
    return fopen( szPath, mode );
}

/* Path accessors used by VitaFileSystem.cpp (szRootDir/szWriteDir are static
 * to this translation unit). Kept in namespace AGK to match the declaration. */
namespace AGK {
    char *AGK_Vita_GetReadDir()  { return szRootDir; }
    char *AGK_Vita_GetWriteDir() { return szWriteDir; }
}

/* AGKThread::Platform* — pthread-backed implementation matching Linux. */
void AGK::AGKThread::PlatformInit()
{
    pThread = new pthread_t; *((pthread_t*)pThread) = 0;
    m_pStop = new pthread_cond_t;

    pthread_condattr_t attr;
    pthread_condattr_init( &attr );
    pthread_condattr_setclock( &attr, CLOCK_MONOTONIC );
    pthread_cond_init( (pthread_cond_t*)m_pStop, &attr );
    pthread_condattr_destroy( &attr );
}

void AGK::AGKThread::PlatformStart()
{
    if ( *((pthread_t*)pThread) ) pthread_detach( *((pthread_t*)pThread) );
    *((pthread_t*)pThread) = 0;
    int result = pthread_create( (pthread_t*)pThread, NULL, (void*(*)(void*))EntryPoint, this );
    if ( result != 0 ) agk::Warning( "Failed to start pthread on Vita" );
}

void AGK::AGKThread::PlatformStop()
{
    pthread_cond_signal( (pthread_cond_t*)m_pStop );
}

void AGK::AGKThread::PlatformTerminate()
{
    if ( m_bRunning ) agk::Warning( "Forcing a thread to terminate, this may cause a crash..." );
}

void AGK::AGKThread::PlatformCleanUp()
{
    if ( m_pStop )
    {
        pthread_cond_destroy( (pthread_cond_t*)m_pStop );
        delete ((pthread_cond_t*)m_pStop);
    }
    m_pStop = 0;
    if ( pThread )
    {
        if ( *((pthread_t*)pThread) ) pthread_detach( *((pthread_t*)pThread) );
        delete (pthread_t*)pThread;
        pThread = 0;
    }
}

void AGK::AGKThread::PlatformJoin()
{
    if ( !pThread || !*((pthread_t*)pThread) ) return;
    pthread_join( *((pthread_t*)pThread), NULL );
    *((pthread_t*)pThread) = 0;
}

void AGK::AGKThread::PlatformSleepSafe( uint32_t milliseconds )
{
    if ( m_bTerminate ) return;
    pthread_mutex_t mutex;
    pthread_mutex_init( &mutex, NULL );
    pthread_mutex_lock( &mutex );

    timespec waittime;
    clock_gettime( CLOCK_MONOTONIC, &waittime );
    waittime.tv_sec  += milliseconds / 1000;
    waittime.tv_nsec += (milliseconds % 1000) * 1000000;

    pthread_cond_timedwait( (pthread_cond_t*)m_pStop, &mutex, &waittime );
    pthread_mutex_unlock( &mutex );
    pthread_mutex_destroy( &mutex );
}

/* cImage::PlatformGetDataFromFile — same path resolution + libpng/libjpeg as
 * Linux. The actual decoders are linked in via libpng / libjpeg sources. */
bool AGK::cImage::PlatformGetDataFromFile( const char *szFile, unsigned char **pData,
                                            unsigned int *out_width, unsigned int *out_height )
{
    uString sPath( szFile );
    if ( !agk::GetRealPath( sPath ) )
    {
        uString err; err.Format( "Could not find image: %s", szFile );
        agk::Error( err );
        return false;
    }

    int width = 0, height = 0;
    bool hasAlpha = false;
    bool result   = false;

    const char *szExt = strrchr( szFile, '.' );
    char *szExtL = agk::Lower( szExt );
    bool bIsPNG = szExtL && strcmp( szExtL, ".png" ) == 0;
    delete [] szExtL;

    if ( bIsPNG ) result = loadPngImage ( sPath.GetStr(), width, height, hasAlpha, pData );
    else          result = loadJpegImage( sPath.GetStr(), width, height, hasAlpha, pData );

    if ( !result )
    {
        uString str( "Failed to load raw image ", 100 );
        str.Append( szFile ); str.Append( ", " ); str.Append( sPath.GetStr() );
        agk::Error( str );
        return false;
    }

    if ( out_width  ) *out_width  = width;
    if ( out_height ) *out_height = height;

    if ( !hasAlpha )
    {
        unsigned char *tmp = new unsigned char[ width * height * 4 ];
        for ( int y = 0; y < height; y++ )
            for ( int x = 0; x < width; x++ )
            {
                unsigned int idx = y * width + x;
                tmp[idx*4    ] = (*pData)[idx*3    ];
                tmp[idx*4 + 1] = (*pData)[idx*3 + 1];
                tmp[idx*4 + 2] = (*pData)[idx*3 + 2];
                tmp[idx*4 + 3] = 255;
            }
        delete [] (*pData);
        *pData = tmp;
    }
    return true;
}

/* ===================================================================
 * Real implementations
 * =================================================================== */

void agk::SetWindowPosition( int /*x*/, int /*y*/ ) {}
void agk::SetWindowSize( int /*w*/, int /*h*/, int /*fullscreen*/ ) {}
void agk::SetWindowSize( int /*w*/, int /*h*/, int /*fullscreen*/, int /*allowOverSized*/ ) {}
void agk::SetWindowAllowResize( int /*mode*/ ) {}
void agk::MaximizeWindow() {}
void agk::MinimizeApp()    {}
void agk::RestoreApp()     {}
int  agk::IsPinAppAvailable() { return 0; }
void agk::PinApp( int /*enable*/ ) {}
void agk::SetImmersiveMode( int /*mode*/ ) {}
void agk::SetScreenResolution( int /*w*/, int /*h*/ ) {}
int  agk::IsDarkTheme() { return 0; }

char* agk::GetURLSchemeText()
{
    char* out = new char[1];
    out[0] = 0;
    return out;
}
void agk::ClearURLSchemeText() {}

char* agk::GetDeviceBaseName()
{
    const char *name = "Vita";
    char *out = new char[ strlen(name) + 1 ];
    strcpy( out, name );
    return out;
}

char* agk::GetDeviceType()
{
    const char *name = "playstation_vita";
    char *out = new char[ strlen(name) + 1 ];
    strcpy( out, name );
    return out;
}

int agk::GetDeviceNetworkType() { return 0; }
int agk::GetStorageRemaining( const char* /*p*/ ) { return 0; }
int agk::GetStorageTotal    ( const char* /*p*/ ) { return 0; }

void agk::GetAppName( uString &out ) { out.SetStr( szAppFolderName ); }
char* agk::GetAppName()
{
    char *out = new char[ strlen(szAppFolderName) + 1 ];
    strcpy( out, szAppFolderName );
    return out;
}

char* agk::GetDeviceLanguage()
{
    /* Use SceAppUtil to read the system language; fall back to en. */
    int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    SceAppUtilInitParam     ip; SceAppUtilBootParam bp;
    memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp));
    sceAppUtilInit(&ip, &bp);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);

    const char *code = "en";
    switch ( lang )
    {
        case SCE_SYSTEM_PARAM_LANG_JAPANESE:        code = "ja"; break;
        case SCE_SYSTEM_PARAM_LANG_ENGLISH_US:
        case SCE_SYSTEM_PARAM_LANG_ENGLISH_GB:      code = "en"; break;
        case SCE_SYSTEM_PARAM_LANG_FRENCH:          code = "fr"; break;
        case SCE_SYSTEM_PARAM_LANG_SPANISH:         code = "es"; break;
        case SCE_SYSTEM_PARAM_LANG_GERMAN:          code = "de"; break;
        case SCE_SYSTEM_PARAM_LANG_ITALIAN:         code = "it"; break;
        case SCE_SYSTEM_PARAM_LANG_DUTCH:           code = "nl"; break;
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:   code = "pt"; break;
        case SCE_SYSTEM_PARAM_LANG_RUSSIAN:         code = "ru"; break;
        case SCE_SYSTEM_PARAM_LANG_KOREAN:          code = "ko"; break;
        case SCE_SYSTEM_PARAM_LANG_CHINESE_T:
        case SCE_SYSTEM_PARAM_LANG_CHINESE_S:       code = "zh"; break;
        case SCE_SYSTEM_PARAM_LANG_POLISH:          code = "pl"; break;
        default: break;
    }
    char *out = new char[ strlen(code) + 1 ];
    strcpy( out, code );
    return out;
}

void agk::SetSleepMode( int /*mode*/ ) {}
void agk::SetExpansionFileKey( const char* /*key*/ ) {}
void agk::SetExpansionFileVersion( int /*v*/ ) {}
int  agk::GetExpansionFileState() { return 0; }
int  agk::GetExpansionFileError() { return 0; }
void agk::DownloadExpansionFile() {}
float agk::GetExpansionFileProgress() { return 0.0f; }
bool agk::ExtractExpansionFile( const char*, const char* ) { return false; }
void agk::SetWindowTitle( const char* /*t*/ ) {}

bool agk::GetDeviceCanRotate()                              { return false; }
void agk::PlatformSetOrientationAllowed( int, int, int, int ) {}

bool agk::PlatformGetDeviceID( uString &out )
{
    /* AppUtil exposes a console-unique ID. Returning empty is safe; analytics
     * that depend on a stable ID can be wired up later. */
    out.SetStr( "" );
    return true;
}

float agk::PlatformDeviceScale()   { return 1.0f; }
int   agk::PlatformGetNumProcessors() { return 4; } /* Vita has 4 user cores */

/* ----- File paths ----- */

static void EnsureDir( const char *path )
{
    sceIoMkdir( path, 0777 );
}

void agk::PlatformInitFilePaths()
{
    /* read-only assets are bundled into app0:/ by the VPK packager */
    /* Use the absolute form WITH the slash. "app0:" alone resolved root-level
     * files but sub-directory paths (app0:Data/Images/...) failed to stat;
     * "app0:/" is the absolute path form and works for nested folders too. */
    strcpy( szBaseDir, "app0:/" );
    strcpy( szRootDir, "app0:/" );

    /* writable storage is per-titleid under ux0:data/. We let SceAppMgr give
     * us the canonical path when available, otherwise build it from the
     * advertised app name. */
    char appName[64];
    appName[0] = 0;
    /* sceAppMgrAppDataMount / sceAppMgrAppParam vary by SDK header; fall
     * back to a generated path. */
    if ( m_sAppName.GetLength() > 0 )
    {
        strncpy( szAppFolderName, m_sAppName.GetStr(), sizeof(szAppFolderName)-1 );
        szAppFolderName[ sizeof(szAppFolderName)-1 ] = 0;
    }

    snprintf( szWriteDir, MAX_PATH, "ux0:data/%s/", szAppFolderName );
    strcpy ( szOriginalWriteDir, szWriteDir );

    /* Create the writable directory tree on first launch. */
    EnsureDir( "ux0:data" );
    EnsureDir( szWriteDir );

    /* Start a fresh diagnostic log — AGK errors/messages are appended here
     * so they can be read after a crash (the Vita has no console). */
    FILE *flog = fopen( "ux0:data/agk_vita_log.txt", "w" );
    if ( flog ) { fprintf( flog, "AGK Vita log\n" ); fclose( flog ); }

    m_bUpdateFileLists = true;
}

void agk::PlatformUpdateWritePath()
{
    if ( m_sAppName.GetLength() == 0 ) { PlatformRestoreWritePath(); return; }

    snprintf( szWriteDir, MAX_PATH, "ux0:data/%s/", szAppFolderName );
    if ( m_sCompanyName.GetLength() > 0 )
    {
        char tmp[ MAX_PATH ];
        snprintf( tmp, MAX_PATH, "ux0:data/%s/%s/", m_sCompanyName.GetStr(), szAppFolderName );
        strcpy( szWriteDir, tmp );
    }
    EnsureDir( szWriteDir );

    m_bUpdateFileLists = true;
}

void agk::PlatformRestoreWritePath()
{
    if ( strlen(szOriginalWriteDir) > 0 ) strcpy( szWriteDir, szOriginalWriteDir );
    m_bUpdateFileLists = true;
}

void agk::OverrideDirectories( const char *szWrite, int /*useRead*/ )
{
    if ( szWrite && *szWrite )
    {
        strcpy( szWriteDir, szWrite );
        cFileEntry::ClearAll();
        m_bUpdateFileLists = true;
    }
    else
    {
        char *szTemp = GetAppName();
        SetAppName( szTemp );
        delete [] szTemp;
    }
}

void agk::InitJoysticks() {}

void agk::PlatformSetDevicePtr( void *ptr ) { SetRendererPointers( ptr ); }

/* ----- Init / time ----- */

void agk::PlatformInitNonGraphicsCommon()
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uFixTime  = (unsigned int)now.tv_sec;
    fFixTime2 = now.tv_nsec * 1e-9;
    SetRandomSeed( uFixTime + (now.tv_nsec % 1000) );

    if ( !m_pMouse[ 0 ] ) m_pMouse[ 0 ] = new cMouse();

    /* Vita has accelerometer + gyro through SceMotion. Off by default — the
     * game enables them via the regular AGK sensor API once we wire it up. */
    agk::m_iAccelerometerExists  = 1;
    agk::m_iGyroSensorExists     = 1;
    agk::m_iMagneticSensorExists = 0;
    agk::m_iLightSensorExists    = 0;
    agk::m_iProximitySensorExists= 0;
    agk::m_iRotationSensorExists = 1;
    m_iGPSSensorExists           = 0;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK,  SCE_TOUCH_SAMPLING_STATE_START);

    /* SceNet init can be added here once the networking layer is fleshed out. */
}

void agk::PlatformInitGraphicsCommon()
{
    float DevToVirX = 1.0f;
    float DevToVirY = 1.0f;
    if ( m_fTargetViewportWidth  > 0 ) DevToVirX = GetVirtualWidth()  / m_fTargetViewportWidth;
    if ( m_fTargetViewportHeight > 0 ) DevToVirY = GetVirtualHeight() / m_fTargetViewportHeight;

    float width  = 250 * DevToVirX;
    float height = 22  * DevToVirY;
    if ( width > GetVirtualWidth() ) width = (float)GetVirtualWidth();

    g_fTextStartX = (GetVirtualWidth()  - width)/2.0f + 3*DevToVirX;
    g_fTextStartY = GetVirtualHeight() / 3.0f + 2*DevToVirY;

    g_pTextInputCursor = new cSprite();
    g_pTextInputCursor->SetSize( 2 * DevToVirX, 18 * DevToVirY );
    g_pTextInputCursor->SetColor( 102, 213, 255, 255 );
    g_pTextInputCursor->SetPosition( g_fTextStartX, g_fTextStartY );
    g_pTextInputCursor->SetOffset( 0,0 );
    g_pTextInputCursor->FixToScreen(1);

    g_pTextInputArea = new cSprite();
    g_pTextInputArea->SetSize( width, height );
    g_pTextInputArea->SetColor( 255,255,255,255 );
    g_pTextInputArea->SetPosition( (GetVirtualWidth() - width) / 2.0f, GetVirtualHeight()/3.0f );
    g_pTextInputArea->SetOffset( 0,0 );
    g_pTextInputArea->FixToScreen(1);

    width  += 8 * DevToVirX;
    height += 8 * DevToVirY;

    g_pTextInputBack = new cSprite();
    g_pTextInputBack->SetSize( width, height );
    g_pTextInputBack->SetColor( 190,190,190,255 );
    g_pTextInputBack->SetPosition( (GetVirtualWidth() - width) / 2.0f, GetVirtualHeight()/3.0f - 4*DevToVirY );
    g_pTextInputBack->SetOffset( 0,0 );
    g_pTextInputBack->FixToScreen(1);

    g_pTextInputText = new cText(30);
    g_pTextInputText->SetPosition( g_fTextStartX, g_fTextStartY );
    g_pTextInputText->SetColor( 0,0,0 );
    g_pTextInputText->SetSpacing( 0 );
    g_pTextInputText->FixToScreen(1);
    g_pTextInputText->SetFont( 0 );

    pTextBackground = new cSprite();
    pTextBackground->SetColor( 0,0,0,128 );
    pTextBackground->SetPosition( -m_iDisplayExtraX, -m_iDisplayExtraY );
    pTextBackground->SetSize( m_iDisplayWidth+m_iDisplayExtraX*2, m_iDisplayHeight+m_iDisplayExtraY*2 );
    pTextBackground->FixToScreen(1);
}

void agk::PlatformInitExternal( void*, int, int, AGKRenderer ) { agk::Error("External mode is not supported on PS Vita"); }

void agk::PlatformInitGraphics( void *ptr, AGKRendererMode rendererMode, uint32_t /*flags*/ )
{
    /* The Core entrypoint has already initialised vitaGL before reaching us.
     * Here we just instantiate and wire the engine's renderer. */
    SetRendererPointers( ptr );

    g_pRenderer = 0;
    switch ( rendererMode )
    {
        case AGK_RENDERER_MODE_ONLY_LOWEST:
        case AGK_RENDERER_MODE_PREFER_BEST:
            g_pRenderer = new OpenGLES2Renderer();
            if ( g_pRenderer->Init() != APP_SUCCESS )
            {
                delete g_pRenderer; g_pRenderer = 0;
                agk::Error( "Failed to initialise OpenGL ES2 (vitaGL)" );
                return;
            }
            break;
        case AGK_RENDERER_MODE_ONLY_ADVANCED:
            agk::Error( "Vulkan renderer is not available on PS Vita" );
            return;
    }

    if ( g_pRenderer->SetupWindow( 0, 0, 960, 544 ) != APP_SUCCESS )
    {
        agk::Error( "Failed to setup renderer window" );
        delete g_pRenderer; g_pRenderer = 0;
        return;
    }

    m_iRealDeviceWidth  = GetSurfaceWidth();
    m_iRealDeviceHeight = GetSurfaceHeight();
    m_iRenderWidth      = m_iRealDeviceWidth;
    m_iRenderHeight     = m_iRealDeviceHeight;
    cCamera::UpdateAllAspectRatio( m_iRenderWidth / (float)m_iRenderHeight );

    if ( g_pRenderer->Setup() != APP_SUCCESS )
    {
        agk::Error( "Failed to setup renderer" );
        return;
    }

    /* vitaGL translates GLSL -> CG (vitashark) and not every shader survives
     * the conversion. Mode 2 makes a failed shader compile non-fatal: the
     * shader is just marked invalid and the game keeps running (without that
     * effect) instead of hitting the AGK fatal-error screen. */
    g_pRenderer->SetShaderErrorMode( 2 );
}

void agk::PlatformInitConsole() {}

void agk::UpdatePtr ( void *ptr ) { SetRendererPointers( ptr ); }
void agk::UpdatePtr2( void * )    {}

int  agk::GetInternalDataI( int /*idx*/ ) { return 0; }

void agk::WindowMoved() {}

void agk::SetVSync( int mode )
{
    if ( g_pRenderer ) g_pRenderer->SetVSync( mode );
}

void agk::Sleep( UINT milliseconds )
{
    sceKernelDelayThread( milliseconds * 1000 );
}

void agk::PlatformCleanUp()
{
    if ( g_pTextInputCursor ) { delete g_pTextInputCursor; g_pTextInputCursor = 0; }
    if ( g_pTextInputArea   ) { delete g_pTextInputArea;   g_pTextInputArea   = 0; }
    if ( g_pTextInputBack   ) { delete g_pTextInputBack;   g_pTextInputBack   = 0; }
    if ( g_pTextInputText   ) { delete g_pTextInputText;   g_pTextInputText   = 0; }
    if ( pTextBackground    ) { delete pTextBackground;    pTextBackground    = 0; }
}

int  agk::GetMaxDeviceWidth () { return 960; }
int  agk::GetMaxDeviceHeight() { return 544; }
int  agk::GetDeviceDPI()       { return 220; } /* approx OLED + LCD model average */

int   agk::GetDisplayNumCutouts()           { return 0; }
float agk::GetDisplayCutoutTop    ( int ) { return 0; }
float agk::GetDisplayCutoutBottom ( int ) { return 0; }
float agk::GetDisplayCutoutLeft   ( int ) { return 0; }
float agk::GetDisplayCutoutRight  ( int ) { return 0; }
float agk::GetScreenBoundsSafeTop()    { return 0; }
float agk::GetScreenBoundsSafeBottom() { return 0; }
float agk::GetScreenBoundsSafeLeft()   { return 0; }
float agk::GetScreenBoundsSafeRight()  { return 0; }

char* agk::GetAppPackageName()
{
    char *out = new char[1]; out[0] = 0; return out;
}

int agk::GetDevicePlatform() { return 7; } /* arbitrary id reserved for Vita */

void agk::PlatformUpdateDeviceSize()
{
    m_iDisplayWidth  = 960;
    m_iDisplayHeight = 544;
}

void agk::PlatformUpdateTime( void )
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int time = now.tv_sec - uFixTime;
    m_fTimeCurr        = time + (now.tv_nsec * 1e-9) - fFixTime2;
    m_iTimeMilliseconds = time * 1000 + (now.tv_nsec / 1000000) - agk::Round(fFixTime2 * 1000);
}

void agk::PlatformResetTime( void )
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uFixTime  = (unsigned int)now.tv_sec;
    fFixTime2 = now.tv_nsec * 1e-9;
    m_fTimeCurr         = 0;
    m_iTimeMilliseconds = 0;
}

double agk::PlatformGetRawTime( void )
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + (now.tv_nsec * 1e-9);
}

void agk::CompositionChanged() {}

void agk::PlatformSync()
{
    /* Vita has no software mouse cursor — nothing to overlay. The actual
     * buffer swap is done by OpenGLES2Renderer::Swap() via vglSwapBuffers. */
}

void agk::PlatformCompleteInputInit()
{
    /* Detection is instant on the Vita — the built-in pad is always
     * present, so expose it as raw joystick #1 right away. */
    cJoystick::DetectJoysticks();
}

void agk::KeyboardMode( int mode ) { m_iKeyboardMode = mode; }

int agk::PlatformInputPointerPressed( float, float ) { return 1; }

/* ----- IME / on-screen keyboard ----- */
static int g_imeRunning = 0;

void agk::PlatformStartTextInput( const char *sInitial )
{
    if ( m_bInputStarted ) return;
    if ( g_imeRunning ) return;

    /* SceWChar16 is uint16_t — u"..." literals are const char16_t which
     * cannot be assigned to a uint16_t array. Build the title manually. */
    static SceWChar16 title[6]  = { 'I','n','p','u','t', 0 };
    static SceWChar16 initial[64];
    static SceWChar16 result[SCE_IME_DIALOG_MAX_TEXT_LENGTH+1];

    /* convert UTF-8 initial -> UTF-16 (best-effort ASCII for now). */
    int i = 0;
    if ( sInitial )
    {
        while ( sInitial[i] && i < 63 ) { initial[i] = (SceWChar16)sInitial[i]; i++; }
    }
    initial[i] = 0;

    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;
    p.languagesForced    = SCE_TRUE;
    p.type               = SCE_IME_TYPE_DEFAULT;
    p.title              = title;
    p.maxTextLength      = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    p.initialText        = initial;
    p.inputTextBuffer    = result;
    sceImeDialogInit(&p);

    g_imeRunning = 1;
    m_bInputStarted = true;
}

void agk::PlatformStopTextInput()
{
    if ( g_imeRunning )
    {
        sceImeDialogTerm();
        g_imeRunning = 0;
    }
    m_bInputStarted = false;
}

void agk::PlatformChangeTextInput( const char* /*str*/ )   {}
void agk::PlatformUpdateTextInput()
{
    if ( !g_imeRunning ) return;
    SceCommonDialogStatus st = sceImeDialogGetStatus();
    if ( st == SCE_COMMON_DIALOG_STATUS_FINISHED )
    {
        SceImeDialogResult r;
        memset(&r, 0, sizeof(r));
        sceImeDialogGetResult(&r);
        sceImeDialogTerm();
        g_imeRunning = 0;
    }
}
void agk::PlatformDrawTextInput()                          {}

void agk::PlatformResumed()       {}
void agk::PlatformResumedOpenGL() {}
void agk::PlatformDeviceVolume()  {}

/* Vita is little endian. */
UINT agk::PlatformLittleEndian( UINT u ) { return u; }
int  agk::PlatformLittleEndian( int  i ) { return i; }
UINT agk::PlatformLocalEndian ( UINT u ) { return u; }
int  agk::PlatformLocalEndian ( int  i ) { return i; }

void agk::PlatformShowChooseScreen()   {}
bool agk::PlatformShowCaptureScreen()  { return false; }
void agk::PlatformHideCaptureScreen()  {}

/* ----- Cameras (none on Vita's public SDK) ----- */
int  agk::GetNumDeviceCameras()                          { return 0; }
int  agk::SetDeviceCameraToImage( UINT, UINT )           { return 0; }
void agk::DeviceCameraUpdate()                           {}
void agk::DeviceCameraResumed()                          {}
int  agk::GetDeviceCameraType( UINT )                    { return 0; }

/* ----- Vibration (rear-pad pulse) ----- */
void agk::VibrateDevice( float /*seconds*/ )
{
    /* Vita does not expose haptics through the public SDK. No-op. */
}

void  agk::SetClipboardText( const char* ) {}
char* agk::GetClipboardText()              { char *o = new char[1]; o[0]=0; return o; }

void agk::PlayYoutubeVideo( const char*, const char*, float ) {}

/* ----- Video playback: not implemented (would need libavplayer wrapper) ---- */
int   agk::LoadVideo( const char* )                      { return 0; }
void  agk::ChangeVideoPointer( void* )                   {}
void  agk::HandleVideoEvents()                           {}
void  agk::DeleteVideo()                                 {}
void  agk::SetVideoDimensions( float, float, float, float ) {}
void  agk::VideoUpdate()                                 {}
void  agk::PlayVideoToImage( UINT )                      {}
void  agk::PlayVideo()                                   {}
void  agk::PauseVideo()                                  {}
void  agk::StopVideo()                                   {}
int   agk::GetVideoPlaying()                             { return 0; }
float agk::GetVideoPosition()                            { return 0.0f; }
float agk::GetVideoDuration()                            { return 0.0f; }
void  agk::SetVideoVolume( float )                       {}
float agk::GetVideoWidth()                               { return 0.0f; }
float agk::GetVideoHeight()                              { return 0.0f; }
void  agk::SetVideoPosition( float )                     {}

void agk::StartScreenRecording( const char*, int ) {}
void agk::StopScreenRecording()                    {}
int  agk::IsScreenRecording()                      { return 0; }

/* ----- Misc unsupported ----- */
void  agk::ActivateSmartWatch( const char* )            {}
int   agk::GetSmartWatchState()                         { return 0; }
void  agk::SendSmartWatchData( const char* )            {}
char* agk::ReceiveSmartWatchData()                      { char *o = new char[1]; o[0]=0; return o; }

void  agk::TextToSpeechSetup()                          {}
int   agk::GetTextToSpeechReady()                       { return 0; }
void  agk::Speak( const char* )                         {}
void  agk::Speak( const char*, int )                    {}
void  agk::SetSpeechRate( float )                       {}
int   agk::GetSpeechNumVoices()                         { return 0; }
char* agk::GetSpeechVoiceLanguage( int )                { char *o = new char[1]; o[0]=0; return o; }
char* agk::GetSpeechVoiceName    ( int )                { char *o = new char[1]; o[0]=0; return o; }
char* agk::GetSpeechVoiceID      ( int )                { char *o = new char[1]; o[0]=0; return o; }
void  agk::SetSpeechLanguage     ( const char* )        {}
void  agk::SetSpeechLanguageByID ( const char* )        {}
int   agk::IsSpeaking()                                 { return 0; }
void  agk::StopSpeaking()                               {}

/* ----- Errors / dialogs ----- */
/* The Vita has no console — append AGK errors/messages to a log file so a
 * crash can be diagnosed afterwards by reading ux0:data/agk_vita_log.txt.
 * Only errors/messages are logged (low volume), not routine activity. */
static void VitaLog( const char *tag, const char *msg )
{
    FILE *f = fopen( "ux0:data/agk_vita_log.txt", "a" );
    if ( f )
    {
        fprintf( f, "[%s] %s\n", tag, msg ? msg : "" );
        fclose( f );
    }
}

void agk::PlatformReportError( const uString &sMsg )
{
    printf( "[AGK ERROR] %s\n", sMsg.GetStr() );
    VitaLog( "ERROR", sMsg.GetStr() );
}

void agk::PlatformMessage( const char *msg )
{
    if ( msg ) printf( "[AGK] %s\n", msg );
    VitaLog( "MSG", msg );
}

/* ----- Paths ----- */
char* agk::GetWritePath()
{
    char *o = new char[ strlen(szWriteDir) + 1 ];
    strcpy( o, szWriteDir );
    return o;
}
char* agk::GetReadPath()
{
    char *o = new char[ strlen(szBaseDir) + 1 ];
    strcpy( o, szBaseDir );
    return o;
}
char* agk::GetDocumentsPath()
{
    return GetWritePath();
}

bool agk::PlatformChooseFile( uString&, const char*, int ) { return false; }

void agk::PlatformGetFullPathWrite( uString &inout )
{
    if ( inout.GetLength() > 0 && (inout[0] == '/' || strncmp(inout, "ux0:", 4) == 0 || strncmp(inout, "app0:", 5) == 0) ) return;
    uString temp; temp.SetStr(szWriteDir); temp.Append(inout);
    inout.SetStr(temp);
}

void agk::PlatformGetFullPathRead( uString &inout, int mode )
{
    if ( inout.GetLength() > 0 && (inout[0] == '/' || strncmp(inout, "ux0:", 4) == 0 || strncmp(inout, "app0:", 5) == 0) ) return;
    if ( mode == 0 )
    {
        uString temp; temp.SetStr(szWriteDir); temp.Append(inout);
        struct stat s;
        if ( stat(temp, &s) == 0 ) { inout.SetStr(temp); return; }
        temp.SetStr(szBaseDir); temp.Append(inout);
        inout.SetStr(temp);
    }
    else
    {
        uString temp; temp.SetStr(szBaseDir); temp.Append(inout);
        inout.SetStr(temp);
    }
}

int agk::PlatformCreateRawPath( const char *path )
{
    /* `path` is a full FILE path. Create every PARENT directory leading to
     * it — but never the final component: that is the file name, and
     * mkdir'ing it would create a directory where the file should go
     * (which is exactly what broke save files: Settings.json became a
     * folder). The loop stops at the last '/', so the file name is left
     * alone for fopen() to create. */
    if ( !path || !*path ) return 0;
    char tmp[ MAX_PATH ];
    strncpy( tmp, path, MAX_PATH-1 ); tmp[ MAX_PATH-1 ] = 0;
    for ( char *p = tmp + 1; *p; p++ )
    {
        if ( *p == '/' )
        {
            *p = 0;
            sceIoMkdir( tmp, 0777 );   /* parent directory only */
            *p = '/';
        }
    }
    return 1;
}

void agk::ParseCurrentDirectory()
{
    /* No CWD on the Vita — paths are always absolute. */
}

int  agk::SetCurrentDir( const char* /*p*/ ) { return 0; }
int  agk::MakeFolder   ( const char *name )
{
    uString full(name);
    PlatformGetFullPathWrite( full );
    return sceIoMkdir( full, 0777 ) >= 0 ? 1 : 0;
}
void agk::DeleteFolder ( const char *name )
{
    uString full(name);
    PlatformGetFullPathWrite( full );
    sceIoRmdir( full );
}

int agk::GetMultiTouchExists() { return 1; }
int agk::GetMouseExists()      { return 0; }
int agk::GetKeyboardExists()   { return 0; }
int agk::GetCameraExists()     { return 0; }

void agk::SetRawMouseVisible( int )      {}
void agk::SetRawMousePosition( float, float ) {}

int agk::GetUnixTime()
{
    SceDateTime t; sceRtcGetCurrentClockUtc(&t);
    time_t epoch;
    sceRtcGetTime_t(&t, &epoch);
    return (int)epoch;
}

int agk::GetDayOfWeek()
{
    time_t t = (time_t)GetUnixTime();
    struct tm *tm = gmtime(&t);
    return tm ? tm->tm_wday + 1 : 0;
}

char* agk::GetCurrentDate()
{
    time_t t = (time_t)GetUnixTime();
    struct tm *tm = localtime(&t);
    char *o = new char[16];
    if (tm) snprintf(o, 16, "%04d-%02d-%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    else    o[0] = 0;
    return o;
}

char* agk::GetCurrentTime()
{
    time_t t = (time_t)GetUnixTime();
    struct tm *tm = localtime(&t);
    char *o = new char[16];
    if (tm) snprintf(o, 16, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    else    o[0] = 0;
    return o;
}

int agk::PlatformGetAdPortal()   { return 0; }
void agk::OpenBrowser( const char *url )
{
    if ( !url ) return;
    sceAppMgrLaunchAppByUri( 0xFFFFF, url );
}

UINT agk::RunApp( const char*, const char* )  { return 0; }
UINT agk::GetAppRunning( UINT )               { return 0; }
void agk::TerminateApp ( UINT )               {}
void agk::ViewFile( const char* )             {}
void agk::ShareText( const char* )            {}
void agk::ShareImage( const char* )           {}
void agk::ShareImageAndText( const char*, const char* ) {}
void agk::ShareFile( const char* )            {}
void agk::FacebookActivateAppTracking()       {}

int  agk::GetInternetState()                  { return 0; }

void agk::SetPushNotificationKeys( const char*, const char* ) {}
int  agk::PushNotificationSetup()                              { return 0; }
char* agk::GetPushNotificationToken()                          { char *o = new char[1]; o[0]=0; return o; }

void agk::PlatformSocialPluginsSetup()                         {}
void agk::PlatformSocialPluginsDestroy()                       {}
void agk::PlatformRateApp( const char*, const char*, const char* ) {}

void  agk::PlatformInAppPurchaseSetKeys ( const char*, const char* ) {}
void  agk::PlatformInAppPurchaseSetTitle( const char* )              {}
char* agk::PlatformGetInAppPurchaseLocalPrice ( int )                 { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformGetInAppPurchaseDescription( int )                 { char *o = new char[1]; o[0]=0; return o; }
void  agk::PlatformInAppPurchaseRestore()                            {}
char* agk::PlatformGetInAppPurchaseSignature(int)                    { char *o = new char[1]; o[0]=0; return o; }

void agk::LoadConsentStatusAdMob( const char*, const char* ) {}
int  agk::GetConsentStatusAdMob() { return 0; }
void agk::RequestConsentAdMob()   {}
void agk::OverrideConsentAdMob( int )      {}
void agk::OverrideConsentChartboost( int ) {}

void agk::PlatformAdMobSetupRelative( const char*, int, int, float, float, int ) {}
void agk::PlatformAdMobRequestNewAd()                 {}
void agk::PlatformAdMobDestroy()                      {}
bool agk::PlatformHasAdMob()                          { return false; }
void agk::PlatformAdMobRewardAd()                     {}
void agk::PlatformAdMobCacheRewardAd()                {}
int  agk::PlatformAdMobGetRewardAdLoaded()            { return 0; }
int  agk::PlatformAdMobGetRewardAdRewarded()          { return 0; }
int  agk::PlatformAdMobGetRewardAdValue()             { return 0; }
void agk::PlatformAdMobResetRewardAd()                {}
void agk::PlatformAdMobSetTesting( int )              {}
void agk::PlatformAdMobSetChildRating( int )          {}

void agk::PlatformChartboostRewardAd()                {}
void agk::PlatformChartboostCacheRewardAd()           {}
int  agk::PlatformChartboostGetRewardAdLoaded()       { return 0; }
int  agk::PlatformChartboostGetRewardAdRewarded()     { return 0; }
void agk::PlatformChartboostResetRewardAd()           {}

char* agk::PlatformFacebookGetFriendsName( int )      { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformFacebookGetFriendsID  ( int )      { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformGetFacebookDownloadFile()          { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformFacebookGetUserID()                { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformFacebookGetUserName()              { char *o = new char[1]; o[0]=0; return o; }
char* agk::PlatformFacebookGetAccessToken()           { char *o = new char[1]; o[0]=0; return o; }

void agk::PlatformCreateLocalNotification( int, int, const char*, const char* ) {}
void agk::PlatformCancelLocalNotification( int )      {}
int  agk::GetNotificationType()                       { return 0; }
void agk::SetNotificationImage( int )                 {}
void agk::SetNotificationText ( const char* )         {}

int  agk::GetNFCExists()                              { return 0; }
UINT agk::GetRawNFCCount()                            { return 0; }
UINT agk::GetRawFirstNFCDevice()                      { return 0; }
UINT agk::GetRawNextNFCDevice ()                      { return 0; }
char* agk::GetRawNFCName( UINT )                      { char *o = new char[1]; o[0]=0; return o; }
int  agk::SendRawNFCData( UINT, const char* )         { return 0; }
int  agk::GetRawNFCDataState( UINT )                  { return 0; }
char* agk::GetRawNFCData( UINT )                      { char *o = new char[1]; o[0]=0; return o; }

int   agk::GetGPSSensorExists()       { return 0; }
void  agk::StartGPSTracking()         {}
void  agk::StopGPSTracking()          {}
float agk::GetRawGPSLatitude()        { return 0.0f; }
float agk::GetRawGPSLongitude()       { return 0.0f; }
float agk::GetRawGPSAltitude()        { return 0.0f; }

int   agk::GetGameCenterExists()                            { return 0; }
void  agk::GameCenterSetup()                                {}
void  agk::GameCenterLogin()                                {}
void  agk::GameCenterLogout()                               {}
int   agk::GetGameCenterLoggedIn()                          { return 0; }
char* agk::GetGameCenterPlayerID()                          { char *o = new char[1]; o[0]=0; return o; }
char* agk::GetGameCenterPlayerDisplayName()                 { char *o = new char[1]; o[0]=0; return o; }
void  agk::GameCenterSubmitScore( int, const char* )        {}
void  agk::GameCenterShowLeaderBoard( const char* )         {}
void  agk::GameCenterSubmitAchievement( const char*, int )  {}
void  agk::GameCenterAchievementsShow()                     {}
void  agk::GameCenterAchievementsReset()                    {}

int  agk::CheckPermission ( const char* ) { return 2; } /* 2 = granted */
void agk::RequestPermission( const char* ) {}

void  agk::SetupCloudData( const char* )                       {}
int   agk::GetCloudDataAllowed()                               { return 0; }
int   agk::GetCloudDataChanged()                               { return 0; }
char* agk::GetCloudDataVariable( const char*, const char *def) { const char *s = def?def:""; char *o = new char[strlen(s)+1]; strcpy(o,s); return o; }
void  agk::SetCloudDataVariable( const char*, const char* )    {}
void  agk::DeleteCloudDataVariable( const char* )              {}

void  agk::SetSharedVariableAppGroup( const char* )                       {}
void  agk::SaveSharedVariable( const char*, const char* )                 {}
char* agk::LoadSharedVariable( const char*, const char *def )             { const char *s = def?def:""; char *o = new char[strlen(s)+1]; strcpy(o,s); return o; }
void  agk::DeleteSharedVariable( const char* )                            {}

void agk::FirebaseSetup()                              {}
void agk::FirebaseLogEvent( const char* )              {}

/* ARKit/ARCore — no Vita equivalent. */
void  agk::ARSetup()                                   {}
int   agk::ARGetStatus()                               { return 0; }
void  agk::ARUpdateInternal()                          {}
void  agk::ARPause()                                   {}
void  agk::ARResume()                                  {}
void  agk::ARDestroy()                                 {}
void  agk::ARControlCamera()                           {}
void  agk::ARDrawBackground()                          {}
void  agk::ARSetPlaneDetectionMode( int )              {}
void  agk::ARSetLightEstimationMode( int )             {}
float agk::ARGetLightEstimate()                        { return 0.0f; }
int   agk::ARHitTest( float, float )                   { return 0; }
float agk::ARGetHitTestX( int )                        { return 0.0f; }
float agk::ARGetHitTestY( int )                        { return 0.0f; }
float agk::ARGetHitTestZ( int )                        { return 0.0f; }
float agk::ARGetHitTestNormalX( int )                  { return 0.0f; }
float agk::ARGetHitTestNormalY( int )                  { return 0.0f; }
float agk::ARGetHitTestNormalZ( int )                  { return 0.0f; }
int   agk::ARGetHitTestType( int )                     { return 0; }
void  agk::ARHitTestFinish()                           {}
int   agk::ARGetPlanes( int )                          { return 0; }
float agk::ARGetPlaneX( int )                          { return 0.0f; }
float agk::ARGetPlaneY( int )                          { return 0.0f; }
float agk::ARGetPlaneZ( int )                          { return 0.0f; }
float agk::ARGetPlaneAngleX( int )                     { return 0.0f; }
float agk::ARGetPlaneAngleY( int )                     { return 0.0f; }
float agk::ARGetPlaneAngleZ( int )                     { return 0.0f; }
float agk::ARGetPlaneSizeX( int )                      { return 0.0f; }
float agk::ARGetPlaneSizeZ( int )                      { return 0.0f; }
void  agk::ARGetPlanesFinish()                         {}
int   agk::ARCreateAnchorFromHitTest( int )            { return 0; }
int   agk::ARCreateAnchorFromPlane  ( int )            { return 0; }
void  agk::ARFixObjectToAnchor( int, int )             {}
int   agk::ARGetAnchorStatus( int )                    { return 0; }
void  agk::ARDeleteAnchor( int )                       {}

int  agk::GetAppInstalled( const char* )                                                          { return 0; }
void agk::SetSnapChatStickerSettings( float, float, int, int, float )                             {}
void agk::ShareSnapChatImage( const char*, const char*, const char*, const char* )                {}
int  agk::ExternalSDKSupported( const char* )                                                     { return 0; }
void agk::ExternalCommand( const char*, const char*, const char*, const char* )                   {}
int  agk::ExternalCommandInt( const char*, const char*, const char*, const char* )                { return 0; }
float agk::ExternalCommandFloat( const char*, const char*, const char*, const char* )             { return 0.0f; }
char* agk::ExternalCommandString( const char*, const char*, const char*, const char* )            { char *o = new char[1]; o[0]=0; return o; }

/* ===================================================================
 * Ads / social / IAP — no Vita equivalent, deliberately empty stubs.
 * (User confirmed these commands are not needed for the Vita port.)
 * =================================================================== */
void agk::PlatformAdMobFullscreen()                                {}
void agk::PlatformAdMobCacheFullscreen()                           {}
void agk::PlatformAdMobPosition( int, int, float, float )          {}
void agk::PlatformSetAdMobVisible( int )                           {}
int  agk::PlatformAdMobGetFullscreenLoaded()                       { return 0; }

void agk::PlatformChartboostSetup()                                {}
void agk::PlatformChartboostFullscreen()                           {}
int  agk::PlatformChartboostGetFullscreenLoaded()                  { return 0; }

void  agk::PlatformFacebookSetup( const char* )                                                      {}
void  agk::PlatformFacebookLogin()                                                                   {}
void  agk::PlatformFacebookLogout()                                                                  {}
void  agk::PlatformFacebookShowLikeButton( const char*, int, int, int, int )                         {}
void  agk::PlatformFacebookDestroyLikeButton()                                                       {}
void  agk::PlatformFacebookPostOnMyWall( const char*, const char*, const char*, const char*, const char* ) {}
void  agk::PlatformFacebookPostOnFriendsWall( const char*, const char*, const char*, const char*, const char*, const char* ) {}
void  agk::PlatformFacebookInviteFriend( const char*, const char* )                                  {}
void  agk::PlatformFacebookGetFriends()                                                              {}
int   agk::PlatformFacebookGetFriendsState()                                                         { return 0; }
int   agk::PlatformFacebookGetFriendsCount()                                                         { return 0; }
void  agk::PlatformFacebookDownloadFriendsPhoto( int )                                               {}
int   agk::PlatformGetFacebookDownloadState()                                                        { return 0; }
int   agk::PlatformGetFacebookLoggedIn()                                                             { return 0; }

void agk::PlatformInAppPurchaseAddProductID( const char*, int )    {}
void agk::PlatformInAppPurchaseSetup()                             {}
void agk::PlatformInAppPurchaseActivate( int )                     {}
int  agk::PlatformGetInAppPurchaseAvailable( int )                 { return 0; }

void agk::PlatformTwitterSetup( const char*, const char* )         {}
void agk::PlatformTwitterMessage( const char* )                    {}

/* Network address — returns failure until SceNet is wired up. */
int agk::PlatformGetIP( uString &sIP )                  { sIP.SetStr(""); return 0; }
int agk::PlatformGetIPv6( uString &sIP, int* )          { sIP.SetStr(""); return 0; }
