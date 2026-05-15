/*
 * VitaAudio.cpp — audio subsystem for the AGK Vita port.
 *
 * SOUND EFFECTS (cSoundMgr): real implementation. AGK decodes WAV/OGG files
 * to raw PCM in the cross-platform layer (cSoundFile::m_pRawData + m_fmt);
 * this file owns a software mixer that sums every active instance into one
 * sceAudioOut port on a dedicated thread. Supports 8/16-bit, mono/stereo
 * sources at any sample rate (linear-resampled to 48 kHz), per-instance
 * volume / playback-rate / stereo-balance and looping.
 *
 * MUSIC (cMusicMgr, AGKMusicOGG): still silent stubs — streaming OGG playback
 * is a separate task. See [[agk-vita-build-env]].
 */

#include "agk.h"

#include <psp2/audioout.h>
#include <pthread.h>
#include <math.h>

namespace AGK
{

/* ====================================================================
 * cSoundInst — a single playing sound. Platform-defined (the engine only
 * forward-declares it and stores cSoundInst* in cSoundMgr::m_pSounds).
 * ==================================================================== */
class cSoundInst
{
    public:
        uint32_t m_iID;
        uint32_t m_uLastUsed;
        int      m_iParent;       // parent cSoundFile id
        int      m_iVolume;       // 0..100
        int      m_iLoop;         // 0 = once, 1 = forever, n = n times
        int      m_iLoopCount;
        float    m_fRate;         // playback rate multiplier
        float    m_fBalance;      // -1 left .. +1 right
        cSoundInst *m_pPrevInst;
        cSoundInst *m_pNextInst;

        /* mixer state */
        cSoundMgr::cSoundFile *m_pFile;
        double           m_dPos;       // fractional sample position in source
        volatile bool    m_bFinished;  // set by mixer, reaped by PlatformUpdate

        cSoundInst() { Reset(); m_iID = 0; m_uLastUsed = 0;
                       m_pPrevInst = 0; m_pNextInst = 0; }

        void Reset()
        {
            m_iParent    = 0;
            m_iVolume    = 100;
            m_iLoop      = 0;
            m_iLoopCount = 0;
            m_fRate      = 1.0f;
            m_fBalance   = 0.0f;
            m_pFile      = 0;
            m_dPos       = 0.0;
            m_bFinished  = false;
        }
};

/* ====================================================================
 * sceAudioOut mixer
 * ==================================================================== */
#define AGK_VITA_AUDIO_RATE   48000
#define AGK_VITA_AUDIO_GRAIN  1024          /* frames per output call */

static SceUID         g_audioPort   = -1;
static pthread_t      g_audioThread;
static pthread_mutex_t g_audioMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int   g_audioRun    = 0;
static int16_t        g_outBuf[ AGK_VITA_AUDIO_GRAIN * 2 ];
static int32_t        g_mixBuf[ AGK_VITA_AUDIO_GRAIN * 2 ];

/* Fetch source frame `idx` from a cSoundFile as floats in [-1,1].
 * Handles 8-bit unsigned / 16-bit signed, mono / stereo. */
static inline void fetch_frame( const cSoundMgr::cSoundFile *f, uint32_t idx,
                                float &outL, float &outR )
{
    int ch    = f->m_fmt.nChannels;
    int bits  = f->m_fmt.wBitsPerSample;
    uint32_t frameBytes = (bits / 8) * ch;
    uint32_t total = f->m_uDataSize / frameBytes;
    if ( idx >= total ) { outL = outR = 0.0f; return; }

    const unsigned char *p = f->m_pRawData + idx * frameBytes;
    float l, r;
    if ( bits == 16 )
    {
        const int16_t *s = (const int16_t*)p;
        l = s[0] / 32768.0f;
        r = (ch == 2) ? s[1] / 32768.0f : l;
    }
    else /* 8-bit unsigned */
    {
        l = (p[0] - 128) / 128.0f;
        r = (ch == 2) ? (p[1] - 128) / 128.0f : l;
    }
    outL = l; outR = r;
}

/* The mixer loop: runs on its own thread, paced by sceAudioOutOutput which
 * blocks until the previous grain has been consumed. */
static void *audio_thread( void * )
{
    while ( g_audioRun )
    {
        for ( int i = 0; i < AGK_VITA_AUDIO_GRAIN * 2; i++ ) g_mixBuf[i] = 0;

        pthread_mutex_lock( &g_audioMutex );

        int globalVol = cSoundMgr::GetVolume();   /* 0..100 */

        cSoundInst *inst = cSoundMgr::GetCurrentSounds();
        while ( inst )
        {
            cSoundInst *next = inst->m_pNextInst;
            cSoundMgr::cSoundFile *f = inst->m_pFile;
            if ( !f || !f->m_pRawData || inst->m_bFinished ) { inst = next; continue; }

            uint32_t frameBytes = (f->m_fmt.wBitsPerSample / 8) * f->m_fmt.nChannels;
            uint32_t total = frameBytes ? f->m_uDataSize / frameBytes : 0;
            if ( total == 0 ) { inst->m_bFinished = true; inst = next; continue; }

            double step = ( (double)f->m_fmt.nSamplesPerSec / AGK_VITA_AUDIO_RATE )
                          * inst->m_fRate;

            float vol = (globalVol / 100.0f) * (inst->m_iVolume / 100.0f);
            float gainL = vol * ( inst->m_fBalance > 0 ? 1.0f - inst->m_fBalance : 1.0f );
            float gainR = vol * ( inst->m_fBalance < 0 ? 1.0f + inst->m_fBalance : 1.0f );

            double pos = inst->m_dPos;
            for ( int s = 0; s < AGK_VITA_AUDIO_GRAIN; s++ )
            {
                if ( pos >= total )
                {
                    /* reached the end */
                    if ( inst->m_iLoop == 1 ||
                         (inst->m_iLoop > 1 && inst->m_iLoopCount + 1 < inst->m_iLoop) )
                    {
                        inst->m_iLoopCount++;
                        pos -= total;
                        if ( pos < 0 ) pos = 0;
                    }
                    else
                    {
                        inst->m_bFinished = true;
                        break;
                    }
                }

                uint32_t i0 = (uint32_t)pos;
                float frac = (float)(pos - i0);
                float l0, r0, l1, r1;
                fetch_frame( f, i0,     l0, r0 );
                fetch_frame( f, i0 + 1, l1, r1 );
                float l = l0 + (l1 - l0) * frac;
                float r = r0 + (r1 - r0) * frac;

                g_mixBuf[ s*2     ] += (int32_t)( l * gainL * 32767.0f );
                g_mixBuf[ s*2 + 1 ] += (int32_t)( r * gainR * 32767.0f );

                pos += step;
            }
            inst->m_dPos = pos;
            inst = next;
        }

        /* mix every active music stream (declared after this function) */
        extern void VitaAudio_MixAllMusic();
        VitaAudio_MixAllMusic();

        pthread_mutex_unlock( &g_audioMutex );

        /* clamp accumulator to int16 */
        for ( int i = 0; i < AGK_VITA_AUDIO_GRAIN * 2; i++ )
        {
            int32_t v = g_mixBuf[i];
            if ( v >  32767 ) v =  32767;
            if ( v < -32768 ) v = -32768;
            g_outBuf[i] = (int16_t)v;
        }

        if ( g_audioPort >= 0 )
            sceAudioOutOutput( g_audioPort, g_outBuf );
    }
    return 0;
}

/* ---- cSoundMgr platform interface --------------------------------- */

void cSoundMgr::PlatformInit()
{
    m_fMinPlaybackRate  = 0.25f;
    m_fMaxPlaybackRate  = 4.0f;
    m_fStepPlaybackRate = 0.01f;

    g_audioPort = sceAudioOutOpenPort( SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                       AGK_VITA_AUDIO_GRAIN,
                                       AGK_VITA_AUDIO_RATE,
                                       SCE_AUDIO_OUT_MODE_STEREO );
    if ( g_audioPort < 0 )
    {
        agk::Error( "Failed to open Vita audio port" );
        return;
    }

    g_audioRun = 1;
    pthread_create( &g_audioThread, 0, audio_thread, 0 );
}

void cSoundMgr::PlatformCleanUp()
{
    if ( g_audioRun )
    {
        g_audioRun = 0;
        pthread_join( g_audioThread, 0 );
    }
    if ( g_audioPort >= 0 )
    {
        sceAudioOutReleasePort( g_audioPort );
        g_audioPort = -1;
    }

    cSoundInst *s = m_pSounds;
    while ( s ) { cSoundInst *n = s->m_pNextInst; delete s; s = n; }
    m_pSounds = 0;
    s = m_pUsedSounds;
    while ( s ) { cSoundInst *n = s->m_pNextInst; delete s; s = n; }
    m_pUsedSounds = 0;
}

void cSoundMgr::PlatformAddFile( cSoundFile * /*pSound*/ )
{
    /* The cross-platform layer already decoded the file to PCM — nothing
     * extra to do per-file on the Vita. */
}

void cSoundMgr::AppPaused()  {}
void cSoundMgr::AppResumed() {}

uint32_t cSoundMgr::PlatformCreateInstance( cSoundFile *pSound, int iVol, int iLoop, int iPriority )
{
    (void)iPriority;
    if ( !pSound ) return 0;
    if ( iVol < 0 ) iVol = 0;
    if ( iVol > 100 ) iVol = 100;
    if ( iLoop < 0 ) iLoop = 0;

    if ( pSound->m_fmt.nChannels < 1 || pSound->m_fmt.nChannels > 2 ||
         (pSound->m_fmt.wBitsPerSample != 8 && pSound->m_fmt.wBitsPerSample != 16) )
    {
        agk::Error( "Unsupported WAV format (only 8/16-bit mono/stereo)" );
        return 0;
    }

    static uint32_t s_nextID = 1;

    cSoundInst *inst = 0;
    if ( m_pUsedSounds )       /* recycle a finished instance */
    {
        inst = m_pUsedSounds;
        m_pUsedSounds = m_pUsedSounds->m_pNextInst;
        if ( m_pUsedSounds ) m_pUsedSounds->m_pPrevInst = 0;
    }
    if ( !inst ) inst = new cSoundInst();

    inst->Reset();
    inst->m_iID     = s_nextID++;
    inst->m_iParent = pSound->m_iID;
    inst->m_iVolume = iVol;
    inst->m_iLoop   = iLoop;
    inst->m_pFile   = pSound;
    inst->m_dPos    = 0.0;
    inst->m_bFinished = false;

    pthread_mutex_lock( &g_audioMutex );
    inst->m_pPrevInst = 0;
    inst->m_pNextInst = m_pSounds;
    if ( m_pSounds ) m_pSounds->m_pPrevInst = inst;
    m_pSounds = inst;
    pthread_mutex_unlock( &g_audioMutex );

    pSound->m_iInstances++;
    return inst->m_iID;
}

void cSoundMgr::PlatformStopInstances( uint32_t iID )
{
    pthread_mutex_lock( &g_audioMutex );
    cSoundInst *s = m_pSounds;
    while ( s )
    {
        cSoundInst *next = s->m_pNextInst;
        if ( iID == 0 || (uint32_t)s->m_iParent == iID )
        {
            if ( m_pSoundFiles[ s->m_iParent ] ) m_pSoundFiles[ s->m_iParent ]->m_iInstances = 0;

            if ( s->m_pPrevInst ) s->m_pPrevInst->m_pNextInst = s->m_pNextInst;
            else m_pSounds = s->m_pNextInst;
            if ( s->m_pNextInst ) s->m_pNextInst->m_pPrevInst = s->m_pPrevInst;

            s->Reset();
            s->m_uLastUsed = agk::GetSeconds();
            s->m_pPrevInst = 0;
            s->m_pNextInst = m_pUsedSounds;
            if ( m_pUsedSounds ) m_pUsedSounds->m_pPrevInst = s;
            m_pUsedSounds = s;
        }
        s = next;
    }
    pthread_mutex_unlock( &g_audioMutex );
}

void cSoundMgr::StopInstance( uint32_t instance )
{
    pthread_mutex_lock( &g_audioMutex );
    cSoundInst *s = m_pSounds;
    while ( s && s->m_iID != instance ) s = s->m_pNextInst;
    if ( s )
    {
        if ( m_pSoundFiles[ s->m_iParent ] ) m_pSoundFiles[ s->m_iParent ]->m_iInstances--;

        if ( s->m_pPrevInst ) s->m_pPrevInst->m_pNextInst = s->m_pNextInst;
        else m_pSounds = s->m_pNextInst;
        if ( s->m_pNextInst ) s->m_pNextInst->m_pPrevInst = s->m_pPrevInst;

        s->Reset();
        s->m_uLastUsed = agk::GetSeconds();
        s->m_pPrevInst = 0;
        s->m_pNextInst = m_pUsedSounds;
        if ( m_pUsedSounds ) m_pUsedSounds->m_pPrevInst = s;
        m_pUsedSounds = s;
    }
    pthread_mutex_unlock( &g_audioMutex );
}

void cSoundMgr::PlatformUpdate()
{
    /* Reap instances the mixer flagged as finished. */
    pthread_mutex_lock( &g_audioMutex );
    cSoundInst *s = m_pSounds;
    while ( s )
    {
        cSoundInst *next = s->m_pNextInst;
        if ( s->m_bFinished )
        {
            s->m_iLoopCount++;
            if ( m_pSoundFiles[ s->m_iParent ] ) m_pSoundFiles[ s->m_iParent ]->m_iInstances--;

            if ( s->m_pPrevInst ) s->m_pPrevInst->m_pNextInst = s->m_pNextInst;
            else m_pSounds = s->m_pNextInst;
            if ( s->m_pNextInst ) s->m_pNextInst->m_pPrevInst = s->m_pPrevInst;

            s->Reset();
            s->m_uLastUsed = agk::GetSeconds();
            s->m_pPrevInst = 0;
            s->m_pNextInst = m_pUsedSounds;
            if ( m_pUsedSounds ) m_pUsedSounds->m_pPrevInst = s;
            m_pUsedSounds = s;
        }
        s = next;
    }
    pthread_mutex_unlock( &g_audioMutex );

    /* Free recycle-list instances idle for >10 s. */
    int now = agk::GetSeconds();
    s = m_pUsedSounds;
    while ( s )
    {
        cSoundInst *next = s->m_pNextInst;
        if ( now - (int)s->m_uLastUsed > 10 )
        {
            if ( s->m_pPrevInst ) s->m_pPrevInst->m_pNextInst = s->m_pNextInst;
            else m_pUsedSounds = s->m_pNextInst;
            if ( s->m_pNextInst ) s->m_pNextInst->m_pPrevInst = s->m_pPrevInst;
            delete s;
        }
        s = next;
    }
}

void cSoundMgr::PlatformUpdateVolume()
{
    /* Volume is read live by the mixer each grain — nothing to push. */
}

void cSoundMgr::SetInstanceVolume( uint32_t instance, int vol )
{
    if ( vol < 0 ) vol = 0;
    if ( vol > 100 ) vol = 100;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { s->m_iVolume = vol; break; }
    pthread_mutex_unlock( &g_audioMutex );
}

void cSoundMgr::SetInstanceRate( uint32_t instance, float rate )
{
    if ( rate < m_fMinPlaybackRate ) rate = m_fMinPlaybackRate;
    if ( rate > m_fMaxPlaybackRate ) rate = m_fMaxPlaybackRate;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { s->m_fRate = rate; break; }
    pthread_mutex_unlock( &g_audioMutex );
}

void cSoundMgr::SetInstanceBalance( uint32_t instance, float balance )
{
    if ( balance < -1 ) balance = -1;
    if ( balance >  1 ) balance =  1;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { s->m_fBalance = balance; break; }
    pthread_mutex_unlock( &g_audioMutex );
}

int cSoundMgr::GetInstanceVolume( uint32_t instance )
{
    int v = 0;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { v = s->m_iVolume; break; }
    pthread_mutex_unlock( &g_audioMutex );
    return v;
}

float cSoundMgr::GetInstanceRate( uint32_t instance )
{
    float r = 0;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { r = s->m_fRate; break; }
    pthread_mutex_unlock( &g_audioMutex );
    return r;
}

int cSoundMgr::GetInstancePlaying( uint32_t instance )
{
    int playing = 0;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { playing = s->m_bFinished ? 0 : 1; break; }
    pthread_mutex_unlock( &g_audioMutex );
    return playing;
}

int cSoundMgr::GetInstanceLoopCount( uint32_t instance )
{
    int c = 0;
    pthread_mutex_lock( &g_audioMutex );
    for ( cSoundInst *s = m_pSounds; s; s = s->m_pNextInst )
        if ( s->m_iID == instance ) { c = s->m_iLoopCount; break; }
    pthread_mutex_unlock( &g_audioMutex );
    return c;
}

/* ====================================================================
 * Music — AGKMusicOGG streaming.
 *
 * The cross-platform layer decodes OGG into m_pDecodeBuffer (16-bit PCM)
 * and calls PlatformAddBuffer; we keep a queue of those buffers and the
 * mixer thread streams them into the same sceAudioOut output, resampled
 * from the file rate to 48 kHz.
 * ==================================================================== */
#define AGK_VITA_MUSIC_MAX_BUFFERS 8

struct VitaMusicBuf
{
    unsigned char *data;
    uint32_t       size;        /* bytes */
    VitaMusicBuf  *next;
};

struct VitaMusicData
{
    VitaMusicBuf  *head;
    VitaMusicBuf  *tail;
    int            numBuffers;
    double         framePos;    /* fractional frame index into head buffer */
    int            channels;    /* of the queued PCM */
    int            rate;        /* sample rate of the queued PCM */
    int            volume;      /* 0..100 */
    volatile bool  playing;
    volatile bool  paused;
    VitaMusicData *nextActive;  /* global playing-music list */
};

/* Global list of music streams the mixer should pull from. */
static VitaMusicData *g_activeMusic = 0;

static void music_free_buffers( VitaMusicData *m )
{
    VitaMusicBuf *b = m->head;
    while ( b ) { VitaMusicBuf *n = b->next; delete [] b->data; delete b; b = n; }
    m->head = m->tail = 0;
    m->numBuffers = 0;
    m->framePos = 0.0;
}

/* Mix one music stream into g_mixBuf. Caller holds g_audioMutex. */
static void mix_music( VitaMusicData *m )
{
    if ( !m || !m->playing || m->paused || !m->head ) return;
    if ( m->channels < 1 || m->channels > 2 || m->rate <= 0 ) return;

    double step = (double)m->rate / AGK_VITA_AUDIO_RATE;
    float  vol  = m->volume / 100.0f;
    int    fb   = 2 * m->channels;             /* bytes per source frame */

    for ( int s = 0; s < AGK_VITA_AUDIO_GRAIN; s++ )
    {
        if ( !m->head ) break;
        uint32_t headFrames = m->head->size / fb;

        /* drop fully-consumed head buffers */
        while ( m->head && (uint32_t)m->framePos >= headFrames )
        {
            VitaMusicBuf *done = m->head;
            m->framePos -= headFrames;
            m->head = m->head->next;
            if ( !m->head ) m->tail = 0;
            m->numBuffers--;
            delete [] done->data; delete done;
            headFrames = m->head ? m->head->size / fb : 0;
        }
        if ( !m->head ) break;

        uint32_t i0 = (uint32_t)m->framePos;
        const int16_t *p = (const int16_t*)( m->head->data + i0 * fb );
        float l, r;
        if ( m->channels == 2 ) { l = p[0] / 32768.0f; r = p[1] / 32768.0f; }
        else                    { l = r = p[0] / 32768.0f; }

        g_mixBuf[ s*2     ] += (int32_t)( l * vol * 32767.0f );
        g_mixBuf[ s*2 + 1 ] += (int32_t)( r * vol * 32767.0f );

        m->framePos += step;
    }
}

/* Helper for AGKMusicOGG::m_pSoundData. */
static inline VitaMusicData *MUSIC( void *p ) { return (VitaMusicData*)p; }

void AGKMusicOGG::PlatformInit()
{
    VitaMusicData *m = new VitaMusicData();
    m->head = m->tail = 0;
    m->numBuffers = 0;
    m->framePos = 0.0;
    m->channels = 2;
    m->rate = 44100;
    m->volume = 100;
    m->playing = false;
    m->paused = false;
    m->nextActive = 0;
    m_pSoundData = m;
}

void AGKMusicOGG::PlatformCleanUp()
{
    if ( !m_pSoundData ) return;
    PlatformStop();
    VitaMusicData *m = MUSIC( m_pSoundData );
    pthread_mutex_lock( &g_audioMutex );
    music_free_buffers( m );
    pthread_mutex_unlock( &g_audioMutex );
    delete m;
    m_pSoundData = 0;
}

int AGKMusicOGG::PlatformPlay()
{
    if ( !m_pSoundData ) return 0;
    VitaMusicData *m = MUSIC( m_pSoundData );

    pthread_mutex_lock( &g_audioMutex );
    m->playing = true;
    m->paused  = false;
    /* add to the active list if not already there */
    bool found = false;
    for ( VitaMusicData *p = g_activeMusic; p; p = p->nextActive )
        if ( p == m ) { found = true; break; }
    if ( !found ) { m->nextActive = g_activeMusic; g_activeMusic = m; }
    pthread_mutex_unlock( &g_audioMutex );
    return 1;
}

void AGKMusicOGG::PlatformStop()
{
    if ( !m_pSoundData ) return;
    VitaMusicData *m = MUSIC( m_pSoundData );

    pthread_mutex_lock( &g_audioMutex );
    m->playing = false;
    m->paused  = false;
    /* unlink from the active list */
    if ( g_activeMusic == m ) g_activeMusic = m->nextActive;
    else for ( VitaMusicData *p = g_activeMusic; p; p = p->nextActive )
        if ( p->nextActive == m ) { p->nextActive = m->nextActive; break; }
    m->nextActive = 0;
    music_free_buffers( m );
    pthread_mutex_unlock( &g_audioMutex );
}

void AGKMusicOGG::PlatformPause()
{
    if ( m_pSoundData ) MUSIC( m_pSoundData )->paused = true;
}

void AGKMusicOGG::PlatformResume()
{
    if ( m_pSoundData ) MUSIC( m_pSoundData )->paused = false;
}

void AGKMusicOGG::PlatformSetVolume()
{
    if ( m_pSoundData ) MUSIC( m_pSoundData )->volume = m_iVolume;
}

void AGKMusicOGG::PlatformClearBuffers()
{
    if ( !m_pSoundData ) return;
    pthread_mutex_lock( &g_audioMutex );
    music_free_buffers( MUSIC( m_pSoundData ) );
    pthread_mutex_unlock( &g_audioMutex );
}

void AGKMusicOGG::PlatformReset()
{
    PlatformClearBuffers();
    if ( m_pSoundData )
    {
        VitaMusicData *m = MUSIC( m_pSoundData );
        m->playing = false;
        m->paused  = false;
    }
}

/* Append the freshly-decoded PCM block (m_pDecodeBuffer / m_iBufferSize). */
int AGKMusicOGG::PlatformAddBuffer( int *reset )
{
    if ( reset ) *reset = 0;
    if ( !m_pSoundData || m_iBufferSize == 0 ) return 1;
    VitaMusicData *m = MUSIC( m_pSoundData );

    VitaMusicBuf *b = new VitaMusicBuf();
    b->size = m_iBufferSize;
    b->data = new unsigned char[ m_iBufferSize ];
    memcpy( b->data, m_pDecodeBuffer, m_iBufferSize );
    b->next = 0;

    pthread_mutex_lock( &g_audioMutex );
    m->channels = (m_fmt.nChannels == 2) ? 2 : 1;
    m->rate     = m_fmt.nSamplesPerSec ? m_fmt.nSamplesPerSec : 44100;
    m->volume   = m_iVolume;
    if ( m->tail ) m->tail->next = b; else m->head = b;
    m->tail = b;
    m->numBuffers++;
    pthread_mutex_unlock( &g_audioMutex );
    return 1;
}

int AGKMusicOGG::PlatformGetNumBuffers()
{
    if ( !m_pSoundData ) return 0;
    return MUSIC( m_pSoundData )->numBuffers;
}

int AGKMusicOGG::PlatformGetMaxBuffers()
{
    return AGK_VITA_MUSIC_MAX_BUFFERS;
}

/* ---- cMusicMgr (legacy non-OGG music) — still silent --------------- *
 * Modern AGK games use the OGG music API above (LoadMusicOGG /
 * PlayMusicOGG). The legacy LoadMusic/PlayMusic path is left silent.   */
void cMusicMgr::PlatformAddFile( cMusic * )                       {}
void cMusicMgr::Play( uint32_t, bool, uint32_t, uint32_t )        {}
void cMusicMgr::Pause()                                           {}
void cMusicMgr::Resume()                                          {}
void cMusicMgr::Stop()                                            {}
void cMusicMgr::HandleEvent()                                     {}
void cMusicMgr::SetMasterVolume( int vol )     { m_iMasterVolume = vol; }
float cMusicMgr::GetDuration( uint32_t )                          { return 0.0f; }
void cMusicMgr::Seek( float, int )                                {}
float cMusicMgr::GetPosition()                                    { return 0.0f; }

/* Called by the mixer thread (g_audioMutex already held) to fold every
 * active music stream into the grain. */
void VitaAudio_MixAllMusic()
{
    for ( VitaMusicData *m = g_activeMusic; m; m = m->nextActive )
        mix_music( m );
}

}
