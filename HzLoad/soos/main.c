#include <3ds.h>

#ifndef _HIMEM
Handle mcuHandle = 0;

Result mcuInit()
{
    return srvGetServiceHandle(&mcuHandle, "mcu::HWC");
}

Result mcuExit()
{
    return svcCloseHandle(mcuHandle);
}

Result mcuWriteRegister(u8 reg, void* data, u32 size)
{
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x20082;
    ipc[1] = reg;
    ipc[2] = size;
    ipc[3] = size << 4 | 0xA;
    ipc[4] = (u32)data;
    Result ret = svcSendSyncRequest(mcuHandle);
    if(ret < 0) return ret;
    return ipc[1];
}
#endif

int main()
{
#if _HIMEM
    while(aptMainLoop())
    {
        svcSleepThread(5e7);
    }
#endif
    
    nsInit();
    
#ifndef _HIMEM
    NS_TerminateProcessTID(0x000401300CF00F02ULL);
    
    hidScanInput();
    
    if(hidKeysHeld() & (KEY_X | KEY_B))
    {
        if(mcuInit() >= 0)
        {
            u8 blk[0x64];
            memset(blk, 0, sizeof(blk));
            blk[2] = 0xFF;
            mcuWriteRegister(0x2D, blk, sizeof(blk));
            mcuExit();
        }
    }
    else
#endif
    {
        u32 pid;
        NS_LaunchTitle(0x000401300CF00F02ULL, 0, &pid);
    }
    
    nsExit();
    
    return 0;
}
