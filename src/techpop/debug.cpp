/**
 * @file debug.cpp
 * @brief Debug routines (debug window, clock measurement display).
 *
 * The Mac build has no debug window, so these routines only keep the Windows
 * behaviour's bookkeeping and produce no visible output.
 */

#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "common.h"
#include "debug.h"

HINSTANCE ghInstance;

// plot signal values on the debug window (not available on the Mac)
int dbgDraw(float l,float r){
    return 0;
}

// formatted output to the debug window
int edPrintf(const char *first,...){
    char szWork[4096];
    va_list args;
    va_start(args,first);
    vsprintf(szWork,first,args);
    va_end(args);
    return 0;
}

void InitDebugWindow(HINSTANCE hInst){
    ghInstance = hInst;
}

// Summarize and display the measured clocks.
void dispClock2(int nSample,LONGLONG clock){
    //// clocks of this time
    char szWork[256];
    float newCl = (float)clock/nSample;
    sprintf(szWork,"処理時間:%6.1f [clk/サンプル]   ",newCl);
    for(int i=9;i<10+4;i++)if(szWork[i]==' ') szWork[i]='0';

    //// average clocks per second
    static const int BUF_SIZE=1024;
    static int sampleSum=0,maxSample,nn;
    static LONGLONG clockSum=0;
    static float maxClock;
    static float clocks[BUF_SIZE];
    char szWork2[256];
    sampleSum += nSample;
    clockSum += clock;
    clocks[nn] = newCl;
    if(maxClock < clocks[nn]){
        maxClock = clocks[nn];
        maxSample = nSample;
    }
    nn++;

    //// one second passed or the buffer is full: display the average
    if(sampleSum>44100 || nn >= BUF_SIZE){
        float aveClock = (float)clockSum/sampleSum;
        float aveSample = (float)sampleSum/nn;

        // variance
        float v=0;
        for(int i=0;i<nn;i++)  {
            float a=(clocks[i]-aveClock)/aveClock;
            a = fabs(a);
            v += a;
        }
        v /= (float)nn;
        v *= 100;

        // average excluding extreme values (more than twice the average)
        float newAve=0;
        int excludeNum=0;
        for(int i=0;i<nn;i++)  {
            float a=(clocks[i]-aveClock)/aveClock;
            a = fabs(a);
            if(a>1.0)excludeNum++;
            else newAve += clocks[i];
        }
        newAve /= (nn-excludeNum);

        sprintf(szWork2,"BlockData平均サイズ:%.1f  平均処理時間:%.1f  極端値除く:%.1f  ワースト:%.1f  分散:%3.1f     ",aveSample,aveClock,newAve,maxClock,v);

        sampleSum=maxSample=nn=0;
        clockSum=0;
        maxClock = 0;
    }
}
