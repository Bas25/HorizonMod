#define _WIN32_WINNT 0x0501
#include <platform.hpp>

extern "C"
{
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <sys/types.h>
#if __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#ifndef WIN32
#include <arpa/inet.h>
#include <netdb.h>
typedef int SOCKET;
typedef struct pollfd WSAPOLLFD;
#endif
#include <poll.h>

#include "inet_pton.h"
}

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
//#include <mstcpip.h>
typedef int socklen_t;
#define errno WSAGetLastError()
#endif

#include <exception>

using ::abs;
using namespace std;

#define FPSNO 5

float fps = 0.0F;
u32 fpsticks[FPSNO];
u32 fpstick = 0;
int currwrite = 0;
int oldwrite = 0;

#define errfail(func) { printf("\n" #func " fail: (%i) %s\n", errno, strerror(errno)); goto killswitch; }


int pollsock(SOCKET sock, int wat, int timeout = 0)
{
#ifndef WIN32
    WSAPOLLFD pd;
    pd.fd = sock;
    pd.events = wat;
    
    if(poll(&pd, 1, timeout) == 1)
        return pd.revents & wat;
#endif
    return 0;
}

class bufsoc
{
public:
    
    struct packet
    {
        u32 packetid : 8;
        u32 size : 24;
        u8 data[0];
    };
    
    SOCKET sock;
    u8* buf;
    int bufsize;
    int recvsize;
    
    bufsoc(SOCKET sock, int bufsize = 1024 * 1024)
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
        int ret = recv(sock, (char*)&hdr, 4, flags);
        if(ret < 0) return -errno;
        if(ret < 4) return -1;
        *(u32*)buf = hdr;
        
        packet* p = pack();
        
        int mustwri = p->size;
        int offs = 4;
        while(mustwri)
        {
            ret = recv(sock, (char*)(buf + offs), mustwri, flags);
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
            ret = send(sock, (char*)(buf + offs) , mustwri, flags);
            if(ret < 0) return -errno;
            mustwri -= ret;
            offs += ret;
        }
        
        return offs;
    }
    
    packet* pack()
    {
        return (packet*)buf;
    }
    
    int errformat(char* c, ...)
    {
        int len = 0;
        
        packet* p = pack();
        
        va_list args;
        va_start(args, c);
        len = vsnprintf((char*)(p->data + 1), 256, c, args);
        va_end(args);
        
        if(len < 0)
        {
            puts("wat");
            return -1;
        }
        
        printf("Packet error %i: %s\n", p->packetid, (char*)(p->data + 1));
        
        p->data[0] = p->packetid;
        p->packetid = 1;
        p->size = (len * sizeof(char)) + 2;
        
        return wribuf();
    }
};


int PumpEvent()
{
    SDL_Event evt;
    
    int i;
    int j;
    
    while(SDL_PollEvent(&evt))
    {
        switch(evt.type)
        {
            case SDL_QUIT:
            case SDL_APP_TERMINATING:
                return 0;
        }
    }
    
    return 1;
}

SDL_Surface* mksurface(int width, int height, int bsiz, int pixfmt)
{
    int rm, gm, bm, am, bs;
    
    switch(pixfmt & 7)
    {
        case 0:
            rm = 0x000000FF;
            gm = 0x0000FF00;
            bm = 0x00FF0000;
            am = 0xFF000000;
            bs = 4;
            break;
        case 2:
            rm = 0xF800;
            gm = 0x07E0;
            bm = 0x001F;
            am = 0;
            bs = 2;
            break;
        case 3:
            rm = 0xF800;
            gm = 0x07C0;
            bm = 0x003E;
            am = 0x0001;
            bs = 2;
            break;
        case 4:
            rm = 0x000F;
            gm = 0x00F0;
            bm = 0x0F00;
            am = 0xF000;
            bs = 2;
            break;
        default:
            rm = 0xFF0000;
            gm = 0x00FF00;
            bm = 0x0000FF;
            am = 0;
            bs = 3;
            break;
    }
    
    printf("Surface: %ix%i %ibpp (%08X %08X %08X %08X)\n", width, height, bs << 3, rm, gm, bm, am);
    SDL_Surface* surf = SDL_CreateRGBSurface(0, width, height, bs << 3, rm, gm, bm, am);
    if(!surf)
    {
        printf("No surface! %s :(\n", SDL_GetError());
    }
    
    return surf;
}


SDL_Window* win = 0;
SDL_Renderer* rendertop = 0;
SDL_Texture* tex[2] = {0, 0};
SDL_Surface* img[2] = {0, 0};


int port = 6464;
SOCKET sock = 0;
struct sockaddr_in sao;
socklen_t sizeof_sao = sizeof(sao);
bufsoc* soc = 0;
bufsoc::packet* p = 0;

u32* pdata = 0;


u8 sbuf[256 * 400 * 4 * 2];
//u8 mehbuf[256 * 400 * 4 * 2];
int srcfmt[2] = {3, 3};
int stride[2] = {480, 480};
int bsiz[2] = {2, 2};
int ret = 0;


int main(int argc, char** argv)
{
    if(argc < 2)
    {
        printf("%s <IP address>\n", argv[0]);
        return 1;
    }
    
    memset(fpsticks, 0, sizeof(fpsticks));
    if(!inet_pton4(argv[1], (unsigned char*)&sao.sin_addr))
    {
        printf("Malformatted IP address: '%s'\n", argv[1]);
        return 1;
    }
    
#ifdef WIN32
    
    WSADATA socHandle;
    
    ret = WSAStartup(MAKEWORD(2,2), &socHandle);
    if(ret)
    {
        printf("WSAStartup failed: %i\n", ret);
        return 1;
    }
    
#endif
    
    sao.sin_family = AF_INET;
    sao.sin_port = htons(port);
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if(sock <= 0) errfail(socket);
    soc = new bufsoc(sock, 0x200000);
    p = soc->pack();
    
    ret = connect(sock, (sockaddr*)&sao, sizeof_sao);
    if(ret < 0) errfail(connect); 
    
    puts("Connected");
    
    //SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    
    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("Failed to init SDL: %s\n", SDL_GetError());
        goto killswitch;
    }
    
    win = SDL_CreateWindow("HorizonScreen " BUILDTIME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 720, 240, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if(!win)
    {
        printf("Can't create window: %s\n", SDL_GetError());
        goto killswitch;
    }
    
    rendertop = SDL_CreateRenderer(win, -1, /*SDL_RENDERER_PRESENTVSYNC*/0);
    
    img[0] = mksurface(240, 400, 3, 1);
    img[1] = mksurface(240, 320, 3, 1);
    
    SDL_RenderSetLogicalSize(rendertop, 720, 240);
    
    do
    {
        int i = sizeof(sbuf) >> 2;
        u32* _1 = (u32*)sbuf;
        while(i--)
        {
            *(_1++) = rand();
        }
    }
    while(0);
    
    while(PumpEvent())
    {
        if(!soc->avail()) goto nocoffei;
        
        ret = soc->readbuf();
        if(ret <= 0) errfail(soc->readbuf);
        
        switch(p->packetid)
        {
            case 2:
                pdata = (u32*)p->data;
                
                printf("ModeTOP: %04X (o: %i, bytesize: %i)\n", pdata[0], pdata[0] & 7, pdata[1]);
                printf("ModeBOT: %04X (o: %i, bytesize: %i)\n", pdata[2], pdata[2] & 7, pdata[3]);
                
                srcfmt[0] = pdata[0];
                stride[0] = pdata[1];
                srcfmt[1] = pdata[2];
                stride[1] = pdata[3];
                
                bsiz[0] = stride[0] / 240;
                bsiz[1] = stride[1] / 240;
                
                SDL_FreeSurface(img[0]);
                SDL_FreeSurface(img[1]);
                
                img[0] = mksurface(stride[0] / bsiz[0], 400, bsiz[0], srcfmt[0]);
                img[1] = mksurface(stride[1] / bsiz[1], 320, bsiz[1], srcfmt[1]);
                break;
            
            case 3:
            {
                memcpy(sbuf + *(u32*)p->data, p->data + 4, p->size);
                
                if(!*(u32*)p->data)
                {
                    u32 prev = SDL_GetTicks() - fpstick;
                    fpsticks[currwrite++] = prev;
                    fpstick = SDL_GetTicks();
                    if(currwrite == FPSNO) currwrite = 0;
                }
                break;
            }
            
            case 0xFF:
            {
                printf("DebugMSG (0x%X):", p->size);
                int i = 0;
                while(i < p->size)
                {
                    printf(" %08X", *(u32*)&p->data[i]);
                    i += 4;
                }
                putchar('\n');
                
                break;
            }
            
            default:
                printf("Unknown packet: %i\n", p->packetid);
                break;
        }
        
        
        nocoffei:
        
        if(img[0])
        {
            SDL_LockSurface(img[0]);
            memcpy(img[0]->pixels, sbuf, stride[0] * 400);
            SDL_UnlockSurface(img[0]);
            
            SDL_DestroyTexture(tex[0]);
            tex[0] = SDL_CreateTextureFromSurface(rendertop, img[0]);
        }
        else puts("img[0] nullptr!");
        
        if(img[1])
        {
            SDL_LockSurface(img[1]);
            memcpy(img[1]->pixels, sbuf + (256 * 400 * 4), stride[1] * 320);
            SDL_UnlockSurface(img[1]);
            
            SDL_DestroyTexture(tex[1]);
            tex[1] = SDL_CreateTextureFromSurface(rendertop, img[1]);
        }
        else puts("img[1] nullptr!");
        
        SDL_Point center;
        center.x = 0;
        center.y = 0;
        
        SDL_Rect soos;
        soos.x = 0;
        soos.y = 0;
        soos.w = 240;
        soos.h = 400;
        
        SDL_Rect dest;
        dest.x = 0;
        dest.y = 240;
        dest.w = 240;
        dest.h = 400;
        SDL_RenderCopyEx(rendertop, tex[0], &soos, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
        soos.x = 0;
        soos.y = 0;
        soos.w = 240;
        soos.h = 320;
        
        dest.x = 400;
        dest.y = 240;
        dest.w = 240;
        dest.h = 320;
        SDL_RenderCopyEx(rendertop, tex[1], &soos, &dest, 270.0F, &center, SDL_FLIP_NONE);
        
        SDL_RenderPresent(rendertop);
        
        if(oldwrite != currwrite)
        {
            float currfps = 0.0F;
            for(int i = 0; i != FPSNO; i++) currfps += fpsticks[i];
            currfps /= FPSNO;
            fps = 1000.0F / currfps;
            printf("FPS: %f\n", fps);
            
            oldwrite = currwrite;
        }
    }
    
    killswitch:
    
    if(soc) delete soc;
    
    if(tex[0]) SDL_DestroyTexture(tex[0]);
    if(tex[1]) SDL_DestroyTexture(tex[1]);
    if(img[0]) SDL_FreeSurface(img[0]);
    if(img[1]) SDL_FreeSurface(img[1]);
    
    if(rendertop) SDL_DestroyRenderer(rendertop);    
    if(win) SDL_DestroyWindow(win);
    SDL_Quit();
    
#ifdef WIN32
    WSACleanup();
#endif
    
    return 0;
}
