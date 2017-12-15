#include <3ds.h>

/*
    HorizonM - utility background process for the Horizon operating system
    Copyright (C) 2017 MarcusD (https://github.com/MarcuzD)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

extern "C"
{
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <malloc.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
}

#include <exception>

#define APPMEMTYPE (*(u32*)0x1FF80030)

extern "C" void __system_allocateHeaps(void)
{
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    
    extern u32 __ctru_heap;
    extern u32 __ctru_heap_size;
    extern u32 __ctru_linear_heap;
    extern u32 __ctru_linear_heap_size;
    
    extern int __stacksize__;
    
    u32 tmp = 0;
    Result res = 0;
    
    // Distribute available memory into halves, aligning to page size.
    //u32 size = (osGetMemRegionFree(MEMREGION_SYSTEM) / 2) & 0xFFFFF000;
    __ctru_heap_size = APPMEMTYPE > 5 ? 0x320000 : 0x84000;
    __ctru_linear_heap_size = APPMEMTYPE > 5 ? 0x2A0000 : 0x1000;
    
    if(APPMEMTYPE > 5) __stacksize__ = 0x10000; else __stacksize__ = 0x8000;
    
    //*(u32*)0x00100998 = size;
    
    
    // Allocate the application heap6
    __ctru_heap = 0x08000000;
    res = svcControlMemory(&tmp, __ctru_heap, 0x0, __ctru_heap_size, (MemOp)MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if(res < 0) *(u32*)0x00100100 = res;
    
    // Allocate the linear heap
    //__ctru_linear_heap = 0x14000000;
    //svcControlMemory(&tmp, 0x1C000000 - __ctru_linear_heap_size, 0x0, __ctru_linear_heap_size, (MemOp)MEMOP_FREE, (MemPerm)(0));
    res = svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, (MemOp)MEMOP_ALLOC_LINEAR, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE));
    if(res < 0) *(u32*)0x00100200 = res;
    if(__ctru_linear_heap < 0x10000000) *(u32*)0x00100071 = __ctru_linear_heap;
    
    // Set up newlib heap
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}


extern "C"
{
    u8 gfxThreadID;
    u8* gfxSharedMemory;
    Handle gspEvent, gspSharedMemHandle;
}

void gxInit()
{
    gfxSharedMemory = (u8*)mappableAlloc(0x1000);
    svcCreateEvent(&gspEvent, RESET_ONESHOT);
    GSPGPU_RegisterInterruptRelayQueue(gspEvent, 0x1, &gspSharedMemHandle, &gfxThreadID);
    svcMapMemoryBlock(gspSharedMemHandle, (u32)gfxSharedMemory, (MemPerm)0x3, (MemPerm)0x10000000);
    gxCmdBuf=(u32*)(gfxSharedMemory+0x800+gfxThreadID*0x200);
    gspInitEventHandler(gspEvent, (vu8*) gfxSharedMemory, gfxThreadID);
    gspWaitForVBlank();
}

void gxExit()
{
    gspExitEventHandler();
    svcUnmapMemoryBlock(gspSharedMemHandle, (u32)gfxSharedMemory);
    GSPGPU_UnregisterInterruptRelayQueue();
    svcCloseHandle(gspSharedMemHandle);
    mappableFree(gfxSharedMemory);
    svcCloseHandle(gspEvent);
}


Handle mcuHandle = 0;

Result mcuInit()
{
    return srvGetServiceHandle(&mcuHandle, "mcu::HWC");
}

Result mcuExit()
{
    return svcCloseHandle(mcuHandle);
}

Result mcuReadRegister(u8 reg, void* data, u32 size)
{
    u32* ipc = getThreadCommandBuffer();
    ipc[0] = 0x10082;
    ipc[1] = reg;
    ipc[2] = size;
    ipc[3] = size << 4 | 0xC;
    ipc[4] = (u32)data;
    Result ret = svcSendSyncRequest(mcuHandle);
    if(ret < 0) return ret;
    return ipc[1];
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


typedef struct
{
    u32 ani;
    u8 r[32];
    u8 g[32];
    u8 b[32];
} RGBLedPattern;

RGBLedPattern pat;

void PatApply()
{
    mcuWriteRegister(0x2D, &pat, sizeof(pat));
}

void PatTrigger()
{
    mcuWriteRegister(0x2D, &pat, 4);
}

void PatStay(u32 col)
{
    memset(&pat.r[0], (col >>  0) & 0xFF, 32);
    memset(&pat.g[0], (col >>  8) & 0xFF, 32);
    memset(&pat.b[0], (col >> 16) & 0xFF, 32);
    
    pat.ani = 0xFF0201;
    
    PatApply();
}

void PatPulse(u32 col)
{
    memset(&pat.r[ 0], 0xFF, 4);
    memset(&pat.g[ 0], 0xFF, 4);
    memset(&pat.b[ 0], 0xFF, 4);
    memset(&pat.r[ 4], 0, 4);
    memset(&pat.g[ 4], 0, 4);
    memset(&pat.b[ 4], 0, 4);
    memset(&pat.r[ 8], (col >>  0) & 0xFF, 8);
    memset(&pat.g[ 8], (col >>  8) & 0xFF, 8);
    memset(&pat.b[ 8], (col >> 16) & 0xFF, 8);
    memset(&pat.r[16], 0, 4);
    memset(&pat.g[16], 0, 4);
    memset(&pat.b[16], 0, 4);
    
    pat.ani = 0xFF0306;
    
    PatApply();
}



#define wah() svcSleepThread(1e8)

#define hangmacro()\
{\
    memset(&pat.r[0], 0x7F, 16);\
    memset(&pat.g[0], 0x7F, 16);\
    memset(&pat.b[0], 0x00, 16);\
    memset(&pat.r[16],0x30, 16);\
    memset(&pat.g[16],0x30, 16);\
    memset(&pat.b[16],0x30, 16);\
    pat.ani = 0x0806;\
    PatApply();\
    while(1)\
    {\
        hidScanInput();\
        if(hidKeysHeld() == (KEY_SELECT | KEY_START))\
        {\
            goto killswitch;\
        }\
        wah();\
    }\
}

static int haznet = 0;

int wait4wifi(int ping_once)\
{\
    haznet = 0;\
    while(1)\
    {\
        u32 wifi = 0;\
        hidScanInput();\
        if(hidKeysHeld() == (KEY_SELECT | KEY_START)) return 0;\
        if(ACU_GetWifiStatus(&wifi) >= 0 && wifi) { haznet = 1; break; }\
        if(ping_once) return 0;\
        wah();\
    }\
    return haznet;\
}


int pollsock(int sock, int wat, int timeout = 0)
{
    struct pollfd pd;
    pd.fd = sock;
    pd.events = wat;
    
    if(poll(&pd, 1, timeout) == 1)
        return pd.revents & wat;
    return 0;
}

class bufsoc
{
public:
    
    typedef struct
    {
        u32 packetid : 8;
        u32 size : 24;
        u8 data[0];
    } packet;
    
    int sock;
    u8* buf;
    int bufsize;
    int recvsize;
    
    bufsoc(int sock, int bufsize)
    {
        this->bufsize = bufsize;
        buf = new u8[bufsize];
        
        recvsize = 0;
        this->sock = sock;
    }
    
    ~bufsoc()
    {
        delete[] buf;
    }
    
    int avail()
    {
        return pollsock(sock, POLLIN) == POLLIN;
    }
    
    int readbuf(int flags = 0)
    {
        u32 hdr = 0;
        int ret = recv(sock, &hdr, 4, flags);
        if(ret < 0) return -errno;
        if(ret < 4) return -1;
        *(u32*)buf = hdr;
        
        packet* p = pack();
        
        int mustwri = p->size;
        int offs = 4;
        while(mustwri)
        {
            ret = recv(sock, buf + offs , mustwri, flags);
            if(ret <= 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        recvsize = offs;
        return offs;
    }
    
    int wribuf(int flags = 0)
    {
        int mustwri = pack()->size + 4;
        int offs = 0;
        int ret = 0;
        while(mustwri)
        {
            ret = send(sock, buf + offs , mustwri, flags);
            if(ret < 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        return offs;
    }
    
    int wriptr(u32 siz, void* ptr, int flags = 0)
    {
        packet* p = pack();
        u32 osiz = p->size;
        p->size += siz;
        
        int mustwri = osiz + 4;
        int offs = 0;
        int ret = 0;
        while(mustwri)
        {
            ret = send(sock, buf + offs , mustwri, flags);
            if(ret < 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        mustwri = siz;
        offs = 0;
        
        while(mustwri)
        {
            if(mustwri > 0x1000)
                ret = send(sock, ptr + offs , 0x1000, flags);
            else
                ret = send(sock, ptr + offs , mustwri, flags);
            if(ret < 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        return osiz + siz + 4;
    }
    
    packet* pack()
    {
        return (packet*)buf;
    }
    
    int errformat(char* c, ...)
    {
        char* wat = nullptr;
        int len = 0;
        
        va_list args;
        va_start(args, c);
        len = vasprintf(&wat, c, args);
        va_end(args);
        
        if(len < 0)
        {
            puts("out of memory");
            return -1;
        }
        
        packet* p = pack();
        
        printf("Packet error %i: %s\n", p->packetid, wat);
        
        p->data[0] = p->packetid;
        p->packetid = 1;
        p->size = len + 2;
        strcpy((char*)(p->data + 1), wat);
        delete wat;
        
        return wribuf();
    }
};

static jmp_buf __exc;
static int  __excno;

void _ded()
{
    puts("\e[0m\n\n- The application has crashed\n\n");
    
    try
    {
        throw;
    }
    catch(std::exception &e)
    {
        printf("std::exception: %s\n", e.what());
    }
    catch(Result res)
    {
        printf("Result: %08X\n", res);
        //NNERR(res);
    }
    catch(int e)
    {
        printf("(int) %i\n", e);
    }
    catch(...)
    {
        puts("<unknown exception>");
    }
    
    puts("\n");
    
    PatStay(0xFFFFFF);
    PatPulse(0xFF);
    
    svcSleepThread(1e9);
    
    hangmacro();
    
    killswitch:
    longjmp(__exc, 1);
}


extern "C" u32 __get_bytes_per_pixel(GSPGPU_FramebufferFormats format);

#include "makerave.h"

const int port = 6464;

static u32 kDown = 0;
static u32 kHeld = 0;
static u32 kUp = 0;

static GSPGPU_CaptureInfo capin;

static int isold = 1;

static Result ret = 0;
static int cx = 0;
static int cy = 0;

static u32 offs[2] = {0, 0};
static u32 limit[2] = {1, 1};
static u32 stride[2] = {80, 80};
static u32 format[2] = {0xF00FCACE, 0xF00FCACE};

static int sock = 0;

static struct sockaddr_in sai;
static socklen_t sizeof_sai = sizeof(sai);

static bufsoc* soc = nullptr;

static bufsoc::packet* k = nullptr;

static Thread netthread = 0;
static vu32 threadrunning = 0;

static u32* screenbuf = nullptr;

void netfunc(void* __dummy_arg__)
{
    u32 siz = 0x80;
    
    u32 fbtop = 0x14000000;
    u32 fbbot = 0x14000000;
    
    if(!isold) osSetSpeedupEnable(1);
    
    k = soc->pack(); //Just In Case (tm)
    
    PatStay(0xFF00);
    
    format[0] = 0xF00FCACE; //invalidate
    

    PatPulse(0x7F007F);
    threadrunning = 1;
    while(threadrunning)
    {
        if(soc->avail())
        while(1)
        {
            if((kHeld & (KEY_SELECT | KEY_START)) == (KEY_SELECT | KEY_START))
            {
                delete soc;
                soc = nullptr;
                break;
            }
            
            puts("reading");
            cy = soc->readbuf();
            if(cy <= 0)
            {
                printf("Failed to recvbuf: (%i) %s\n", errno, strerror(errno));
                delete soc;
                soc = nullptr;
                break;
            }
            else
            {
                printf("#%i 0x%X | %i\n", k->packetid, k->size, cy);
                
                reread:
                switch(k->packetid)
                {
                    case 0: //CONNECT
                    case 1: //ERROR
                        puts("forced dc");
                        delete soc;
                        soc = nullptr;
                        break;
                        
                    default:
                        printf("Invalid packet ID: %i\n", k->packetid);
                        delete soc;
                        soc = nullptr;
                        break;
                }
                
                break;
            }
        }
        
        if(!soc) break;
        
        ///if(1)
        if(GSPGPU_ImportDisplayCaptureInfo(&capin) >= 0)
        {
            //GSPGPU_ReadHWRegs(0x400468, &fbtop, 4);
            //GSPGPU_ReadHWRegs(0x400494, &fbtop, 4);
            
            //if((u32)capin.screencapture[0].framebuf0_vaddr < 0x1C000000)
            //    *((u32*)&capin.screencapture[0].framebuf0_vaddr) += 0x1C000000;
            
            if\
            (\
                capin.screencapture[0].format != format[0]\
                ||\
                capin.screencapture[1].format != format[1]\
            )
            {
                format[0] = capin.screencapture[0].format;
                format[1] = capin.screencapture[1].format;
                
                k->packetid = 2; //MODE
                k->size = 4 * 4;
                
                u32* kdata = (u32*)k->data;
                
                kdata[0] = format[0];
                kdata[1] = capin.screencapture[0].framebuf_widthbytesize;
                kdata[2] = format[1];
                kdata[3] = capin.screencapture[1].framebuf_widthbytesize;
                soc->wribuf();
                
                k->packetid = 0xFF;
                k->size = sizeof(capin);
                *(GSPGPU_CaptureInfo*)k->data = capin;
                soc->wribuf();
            }
            
            //if(fbtop >= 0x14008000)
            // yes, I know this indentation is cancer
            // also, I know I'm a lazy fuck for only allowing VRAM framebuffers
            //TODO find a way to read from LINEARmemeory without AcquireRights
            if\
            (\
                (u32)capin.screencapture[0].framebuf0_vaddr >= 0x1F000000\
                 &&\
                (u32)capin.screencapture[0].framebuf0_vaddr <  0x1F600000\
            )
            {
                siz = (capin.screencapture[0].framebuf_widthbytesize * stride[0]);
                
                k->packetid = 3; //DATA
                k->size = siz;
                *(u32*)k->data = siz * offs[0];
                memcpy(k->data + 4, ((u8*)capin.screencapture[0].framebuf0_vaddr) + *(u32*)k->data, siz);
                
                /*
                GSPGPU_AcquireRight(0);
                Result res = GX_TextureCopy\
                (\
                        capin.screencapture[0].framebuf0_vaddr,\
                        0,\
                        screenbuf,\
                        0,\
                        320 * 240 * 4, BIT(3)\
                );
                if(res < 0)
                {
                    *(u32*)0x00100000 = res;
                }
                else svcSleepThread(5e7);//gspWaitForPPF();
                GSPGPU_ReleaseRight();
                
                memcpy(k->data + 4, screenbuf, siz);
                //memcpy(k->data + 4, ((u8*)fbtop) + *(u32*)k->data, siz);
                */
                
                if(++offs[0] == limit[0]) offs[0] = 0;
                k->size += 4;
                soc->wribuf();
                
            }
            else
            {
                k->packetid = 0xFF;
                k->size = 8;
                //*(u32*)k->data = fbtop;
                //*(u32*)&k->data[4] = fbbot;
                *(u32*)k->data = (u32)capin.screencapture[0].framebuf0_vaddr;
                *(u32*)&k->data[4] = (u32)capin.screencapture[1].framebuf0_vaddr;
                soc->wribuf();
                svcSleepThread(1e9);
            }
            
            if\
            (\
                (u32)capin.screencapture[1].framebuf0_vaddr >= 0x1F000000\
                &&\
                (u32)capin.screencapture[1].framebuf0_vaddr <  0x1F600000\
            )
            {
                siz = (capin.screencapture[1].framebuf_widthbytesize * stride[1]);
                
                k->packetid = 3; //DATA
                k->size = siz;
                *(u32*)k->data = siz * offs[1];
                memcpy(k->data + 4, ((u8*)capin.screencapture[1].framebuf0_vaddr) + *(u32*)k->data, siz);
                if(++offs[1] == limit[1]) offs[1] = 0;
                k->size += 4;
                *(u32*)k->data += 256 * 400 * 4;
                soc->wribuf();
                
            }
            
            //svcSleepThread(2e7);
            
            //gspWaitForVBlank();
        }
        else wah();
    }
    
    memset(&pat.r[0], 0xFF, 16);
    memset(&pat.g[0], 0xFF, 16);
    memset(&pat.b[0], 0x00, 16);
    memset(&pat.r[16],0x7F, 16);
    memset(&pat.g[16],0x00, 16);
    memset(&pat.b[16],0x7F, 16);
    pat.ani = 0x0406;
    PatApply();
    
    if(soc)
    {
        delete soc;
        soc = nullptr;
    }
    
    threadrunning = 0;
}

int main()
{
    int hazgui = 0;
    
    mcuInit();
    
    memset(&pat, 0, sizeof(pat));
    memset(&capin, 0, sizeof(capin));
    
    isold = APPMEMTYPE <= 5;
    
    //if(1)
    if(isold)
    {
        limit[0] = 2;
        limit[1] = 2;
        stride[0] = 200;
        stride[1] = 160;
    }
    else
    {
        limit[0] = 1;
        limit[1] = 1;
        stride[0] = 400;
        stride[1] = 320;
    }
    
    
    PatStay(0xFF);
    
    acInit();
    
    do
    {
        u32 siz = isold ? 0x40000 : 0x200000;
        ret = socInit((u32*)memalign(0x1000, siz), siz);
    }
    while(0);
    if(ret < 0) hangmacro();
    
    gspInit();
    
    //gxInit();
    
    screenbuf = (u32*)linearAlloc(400 * 240 * 4);
    
    
    if((__excno = setjmp(__exc))) goto killswitch;
      
#ifdef _3DS
    std::set_unexpected(_ded);
    std::set_terminate(_ded);
#endif
    
    netreset:
    
    if(wait4wifi(1))
    {
        if(errno == EINVAL)
        {
            errno = 0;
            PatStay(0xFFFF);
            while(wait4wifi(1)) wah();
        }
    }
    else PatStay(0xFFFF);
    
    if(wait4wifi(1))
    {
        cy = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if(cy <= 0)
        {
            printf("socket error: (%i) %s\n", errno, strerror(errno));
            hangmacro();
        }
        
        sock = cy;
        
        struct sockaddr_in sao;
        sao.sin_family = AF_INET;
        sao.sin_addr.s_addr = gethostid();
        sao.sin_port = htons(port);
        
        if(bind(sock, (struct sockaddr*)&sao, sizeof(sao)) < 0)
        {
            printf("bind error: (%i) %s\n", errno, strerror(errno));
            hangmacro();
        }
        
        //fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        
        if(listen(sock, 1) < 0)
        {
            printf("listen error: (%i) %s\n", errno, strerror(errno));
            hangmacro();
        }
    }
    
    
    reloop:
    
    if(!isold) osSetSpeedupEnable(1);
    
    if(haznet) PatStay(0xCCFF00);
    
    while(1)
    {
        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();
        kUp = hidKeysUp();
        
        //printf("svcGetSystemTick: %016llX\n", svcGetSystemTick());
        
        if(kDown) PatPulse(0xFF);
        if(kHeld == (KEY_SELECT | KEY_START)) break;
        
        if(!soc)
        {
            if(!haznet)
            {
                if(wait4wifi(1)) goto netreset;
            }
            else if(pollsock(sock, POLLIN) == POLLIN)
            {
                int cli = accept(sock, (struct sockaddr*)&sai, &sizeof_sai);
                if(cli < 0)
                {
                    printf("Failed to accept client: (%i) %s\n", errno, strerror(errno));
                    if(errno == EINVAL) goto netreset;
                }
                else
                {
                    soc = new bufsoc(cli, isold ? 0x2EE10 : 0xC0000);
                    k = soc->pack();
                    
                    if(isold)
                    {
                        netthread = threadCreate(netfunc, nullptr, 0x400, 8, 1, true);
                    }
                    else
                    {
                        netthread = threadCreate(netfunc, nullptr, 0x4000, 8, 3, true);
                    }
                    
                    if(!netthread)
                    {
                        memset(&pat, 0, sizeof(pat));
                        memset(&pat.r[0], 0xFF, 16);
                        pat.ani = 0x102;
                        PatApply();
                        
                        svcSleepThread(2e9);
                    }
                    
                    
                    if(netthread)
                    {
                        while(!threadrunning) wah();
                    }
                    else
                    {
                        delete soc;
                        soc = nullptr;
                        hangmacro();
                    }
                }
            }
            else if(pollsock(sock, POLLERR) == POLLERR)
            {
                printf("POLLERR (%i) %s", errno, strerror(errno));
                goto netreset;
            }
        }
        
        if(netthread && !threadrunning)
        {
            //TODO todo?
            netthread = nullptr;
            goto reloop;
        }
        
        /*if((kDown & (KEY_ZL | KEY_ZR)) && kHeld == (KEY_ZL | KEY_ZR))
        {
            hazgui = !hazgui;
            
            if(hazgui)
            {
                GSPGPU_AcquireRight(0x1);
            }
            else
            {
                GSPGPU_ReleaseRight();
            }
        }*/
        
        if((kHeld & (KEY_ZL | KEY_ZR)) == (KEY_ZL | KEY_ZR))
        {
            u32* ptr = (u32*)0x1F000000;
            int o = 0x00600000 >> 2;
            while(o--) *(ptr++) = rand();
        }
        
        wah();
    }
    
    killswitch:
    
    PatStay(0xFF0000);
    
    if(netthread)
    {
        threadrunning = 0;
        
        while(soc) wah();
    }
    
    if(soc) delete soc;
    else close(sock);
    
    puts("Shutting down sockets...");
    SOCU_ShutdownSockets();
    
    socExit();
    
    //gxExit();
    
    gspExit();
    
    acExit();
    
    PatStay(0);
    
    mcuExit();
    
    return 0;
}
