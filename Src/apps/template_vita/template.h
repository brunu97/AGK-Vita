#ifndef _H_AGKTEMPLATE_VITA_
#define _H_AGKTEMPLATE_VITA_

#include "agk.h"

#define DEVICE_WIDTH  960
#define DEVICE_HEIGHT 544
#define DEVICE_POS_X  0
#define DEVICE_POS_Y  0
#define FULLSCREEN    true

#define COMPANY_NAME  "AGKVita"

class app
{
    public:
        app() { memset( this, 0, sizeof(app) ); }

        void Begin( void );
        int  Loop ( void );
        void End  ( void );
};

extern app App;

#endif

#ifdef LoadImage
 #undef LoadImage
#endif
