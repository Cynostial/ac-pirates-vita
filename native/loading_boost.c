/* CPU boost while the game loads (user-approved for loading only): 444 MHz
 * from launch until the main menu is ready, and while a loading screen is
 * shown; the clock found at launch (stock 333 MHz) at all other times. The
 * GPU and bus clocks are never changed. A watchdog restores the stock clock
 * if a boost outlives any plausible load. */
#include "acp_native.h"
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#define BOOST_MHZ 444
#define WATCHDOG_US (300ull*1000000ull)
static unsigned reasons;
static int stock=-1;
static uint64_t since;
static const char *name(unsigned reason){return reason==ACP_LOADING_LAUNCH?"launch":reason==ACP_LOADING_SCREEN?"loading screen":"other";}
void acp_loading_boost(unsigned reason,int on){
    unsigned next=on?reasons|reason:reasons&~reason;
    if(next==reasons)return;
    if(stock<0)stock=scePowerGetArmClockFrequency();
    int was=reasons!=0;reasons=next;
    if(stock>=BOOST_MHZ)return;
    if(!was&&next){
        scePowerSetArmClockFrequency(BOOST_MHZ);since=sceKernelGetProcessTimeWide();
        acp_log("CPU %d MHz while loading (%s)",scePowerGetArmClockFrequency(),name(reason));
    }else if(was&&!next){
        scePowerSetArmClockFrequency(stock);
        acp_log("CPU %d MHz after %.1f s of loading (%s done)",scePowerGetArmClockFrequency(),
                (sceKernelGetProcessTimeWide()-since)/1e6,name(reason));
    }
}
void acp_loading_boost_check(void){
    if(!reasons||stock<0||stock>=BOOST_MHZ||sceKernelGetProcessTimeWide()-since<WATCHDOG_US)return;
    reasons=0;scePowerSetArmClockFrequency(stock);
    acp_log("CPU %d MHz: loading boost watchdog after %u s",scePowerGetArmClockFrequency(),(unsigned)(WATCHDOG_US/1000000ull));
}
