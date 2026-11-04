/* Real world rendering beyond the native 256x224 field.
 * Map indices are read from the game's live decompressed WRAM pages; metatiles
 * come from the user's ROM. Actor parts follow $03:C849 without its screen
 * cull. The unmodified native raster remains authoritative inside its field.
 */
#include "iog_portrait.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdatomic.h>
#include "flippad/portrait_rules.h"
extern Snes *g_snes;
enum { MAX_W=640,MAX_H=768 };
/* The Japanese Chinese build retains the Japanese player-global layout. */
#ifdef IOG_CHINESE_ROM
#define IOG_PLAYER_ACTOR 0x9b8
#define IOG_STREAM_FLAGS 0x9bc
#define IOG_STREAM_TABLE 0xd99d
#else
#define IOG_PLAYER_ACTOR 0x9aa
#define IOG_STREAM_FLAGS 0x9ae
#define IOG_STREAM_TABLE 0xd974
#endif
static uint32_t image[MAX_W*MAX_H], saved[MAX_W*MAX_H];
static uint8_t depth[MAX_W*MAX_H], subdepth[MAX_W*MAX_H],obj_claimed[MAX_W*MAX_H];
static uint8_t colors[MAX_W*MAX_H], subcolors[MAX_W*MAX_H], sources[MAX_W*MAX_H];
typedef struct {
 uint16_t sx[2],sy[2],tile[2],mapbase[2],mapsize[2],fixed,palette[256],logic;
 uint32_t windows;
 uint8_t supported,main,sub,mainwin,subwin,math,select,display,wl[2],wr[2];
} Raster;
static Raster raster[224];
static int raster_valid;
static uint8_t actor_frame[0x20000];
static int reading_actors;
void IogPortraitLine(int line){
 if(line<1||line>224)return;
 Raster*r=&raster[line-1];
 for(int i=0;i<2;i++){r->sx[i]=g_ppu->hScroll[i];r->sy[i]=g_ppu->vScroll[i];r->tile[i]=PPU_bgTileAdr(g_ppu,i);r->mapbase[i]=PPU_bgTilemapAdr(g_ppu,i);r->mapsize[i]=g_ppu->bgXsc[i]&3;}
 r->supported=fp_supported_background(PPU_mode(g_ppu),PPU_bigTiles(g_ppu,0)||PPU_bigTiles(g_ppu,1),PPU_mosaicSize(g_ppu)>1&&(g_ppu->mosaic&3));
 r->fixed=g_ppu->fixedColor;r->main=g_ppu->screenEnabled[0];r->sub=g_ppu->screenEnabled[1];
 r->mainwin=g_ppu->screenWindowed[0];r->subwin=g_ppu->screenWindowed[1];
 r->math=g_ppu->cgadsub;r->select=g_ppu->cgwsel;r->display=g_ppu->inidisp;
 r->windows=g_ppu->windowsel;r->logic=g_ppu->wbgobjlog;
 r->wl[0]=g_ppu->window1left;r->wr[0]=g_ppu->window1right;r->wl[1]=g_ppu->window2left;r->wr[1]=g_ppu->window2right;
 memcpy(r->palette,g_ppu->cgram,sizeof(r->palette));raster_valid=1;
}
static const Raster* row_state(int y){if(y<0)y=0;if(y>223)y=223;return &raster[y];}
/* The PPU scroll wraps at 1024; choose the world coordinate nearest the
   game's camera, including the fine scroll actually used by this raster. */
static int unwrap(int actual,int camera){return fp_unwrap10(actual,camera);}
static int window(const Raster*r,int layer,int x){return fp_window(r->windows,r->logic,r->wl,r->wr,layer,x);}
static int window_mode(int mode,int inside){return mode==3||(mode==2&&inside)||(mode==1&&!inside);}
static uint32_t shade(const Raster*r,int x,int color,int source,int subcolor,int subhas){
 if(r->display&128)return 0;
 unsigned c=r->palette[color],other=(r->select&2)&&subhas?r->palette[subcolor]:r->fixed;
 int inside=window(r,5,x),clip=window_mode(r->select>>6,inside);
 int math=source<6&&(r->math&(1<<source))&&!window_mode((r->select>>4)&3,inside);
 uint32_t out=0;
 for(int k=0;k<3;k++){
  int v=clip?0:((c>>(5*k))&31);
  if(math){int b=(other>>(5*k))&31;v+=(r->math&128)?-b:b;if((r->math&64)&&(!(r->select&2)||subhas))v>>=1;}
  if(v<0)v=0;if(v>31)v=31;v=((v<<3)|(v>>2))*(r->display&15)/15;out|=(uint32_t)v<<(16-8*k);
 }
 return out;
}
static int saved_w,saved_h,world_map=-1;
static _Atomic float zoom;
static atomic_int filter,safe_edge,pad_mode;
static uint16_t tile_sets[2][1024];
static uint32_t set_pointer[2];
static uint8_t set_valid[2];
static unsigned actors_drawn;
static uint32_t hud_pixels[256*240];
static int hud_active;
static int audit_reconstruct;
static uint16_t word(unsigned a){const uint8_t*r=reading_actors?actor_frame:g_snes->ram;return r[a]|r[a+1]<<8;}
static uint32_t ptr(unsigned a){return word(a)|(uint32_t)g_snes->ram[a+2]<<16;}
static unsigned read_byte(int bank,unsigned adr){
 adr&=65535;
 if(bank==0x7e||bank==0x7f)return (reading_actors?actor_frame:g_snes->ram)[((bank-0x7e)<<16)|adr];
 if(adr<0x2000 && (bank&127)<64)return g_snes->ram[adr];
 unsigned off=((bank&63)<<16)|adr;
 return off<g_snes->cart->romSize?g_snes->cart->rom[off]:0;
}
static unsigned read_word(int bank,unsigned a){return read_byte(bank,a)|read_byte(bank,a+1)<<8;}
static unsigned bits(const uint8_t*r,size_t size,unsigned*b,int n){
 unsigned v=0;for(int i=0;i<n;i++){v<<=1;if(*b<size*8)v|=(r[*b>>3]>>(7-(*b&7)))&1;(*b)++;}return v;
}
static int load_set(int layer){
 uint32_t p=ptr(0x678+3*layer)&0x3fffff;
 if(set_valid[layer]&&set_pointer[layer]==p)return 1;
 const uint8_t*r=g_snes->cart->rom;size_t size=g_snes->cart->romSize;
 if(p+2>=size)return 0;
 unsigned n=r[p]|r[p+1]<<8;
 if(n!=2048)return 0;
 uint8_t dict[256],out[2048];memset(dict,32,256);unsigned di=239,b=(p+2)*8,at=0;
 while(at<n&&b<size*8){
  if(bits(r,size,&b,1)){unsigned v=bits(r,size,&b,8);out[at++]=v;dict[di++&255]=v;}
  else {unsigned q=bits(r,size,&b,8),num=bits(r,size,&b,4)+2;while(num--){uint8_t v=dict[q++&255];if(at<n)out[at++]=v;dict[di++&255]=v;}}
 }
 if(at!=n)return 0;
 for(int i=0;i<1024;i++)tile_sets[layer][i]=out[2*i]|out[2*i+1]<<8;
 set_pointer[layer]=p;set_valid[layer]=1;return 1;
}
static int char_pixel(unsigned addr,int x,int y){
 const uint16_t*v=PpuRenderVram(g_ppu);unsigned a=(addr+y)&32767;
 unsigned b=v[a],c=v[(a+8)&32767],s=7-x;
 return ((b>>s)&1)|(((b>>(s+8))&1)<<1)|(((c>>s)&1)<<2)|(((c>>(s+8))&1)<<3);
}
static int map_word(int layer,int x,int y,unsigned *out){
 int w=word(0x692+2*layer),h=word(0x696+2*layer);
 if(x<0||y<0||x>=w||y>=h||w<256||w>2048||h<256||h>2048)return 0;
 unsigned off=0xa000+layer*0x2000+((y>>8)*(w>>8)+(x>>8))*256+((y>>4)&15)*16+((x>>4)&15);
 if(off>=0xe000)return 0;
 unsigned met=g_snes->ram[off];unsigned a=tile_sets[layer][met*4+((y>>3)&1)*2+((x>>3)&1)];
 *out=a;return 1;
}
static int bg_sample(int layer,int x,int y,int *prio,const Raster*r){
 unsigned a;if(!map_word(layer,x,y,&a))return 0;
 int tx=x&7,ty=y&7;if(a&0x4000)tx=7-tx;if(a&0x8000)ty=7-ty;
 /* BG2 uses the second 256-character page of the shared tile bank. */
 int pix=char_pixel((r->tile[layer]+((a&511)+layer*256)*16)&32767,tx,ty);
 if(!pix)return 0;
 *prio=(a&0x2000)?(layer?7:8):(layer?4:5);
 return pix+((a>>10)&7)*16;
}
/* Reject a loading/unsupported map until the complete-map words agree with
 * the actual native VRAM ring. Never infer scenery from stale dimensions. */
static int map_ready(int secondary){
 const uint16_t*v=PpuRenderVram(g_ppu);
 for(int layer=0;layer<2;layer++){
  int tested=0,enabled=0;
  for(int y=48;y<=208;y+=80)for(int x=8;x<=248;x+=120){
   const Raster*r=row_state(y);if(r->display&128)continue;
   if(!r->supported)return 0;
   if(!((r->main|r->sub)&(1<<layer)))continue;
   enabled=1;if(layer&&!secondary)return 0;
   unsigned ours;int wx=unwrap(r->sx[layer],word(0x68a+2*layer))+x,wy=unwrap(r->sy[layer],word(0x68e +2*layer))+y;
   if(!map_word(layer,wx,wy,&ours))continue;
   unsigned tx=((r->sx[layer]+x)>>3)&63,ty=((r->sy[layer]+y)>>3)&63;
   unsigned adr=r->mapbase[layer]+(tx&31)+(ty&31)*32;
   if((r->mapsize[layer]&1)&&(tx&32))adr+=1024;
   if((r->mapsize[layer]&2)&&(ty&32))adr+=(r->mapsize[layer]&1)?2048:1024;
   unsigned expected=(ours&0xfc00)|(((ours&511)+layer*256)&1023);
   if(expected!=v[adr&32767])return 0;
   tested++;
  }
  if(enabled&&!tested)return 0;
 }
 return 1;
}
static int source_pixel(unsigned bank,unsigned base,unsigned tile,int x,int y){
 unsigned adr=base+tile*32+y*2,bit=7-x;
 return ((read_byte(bank,adr)>>bit)&1)|(((read_byte(bank,adr+1)>>bit)&1)<<1)|(((read_byte(bank,adr+16)>>bit)&1)<<2)|(((read_byte(bank,adr+17)>>bit)&1)<<3);
}
static void actor_parts(int w,int h,int camera_x,int camera_y,int left,int top){
 unsigned a=word(0x58),active[128],n=0;actors_drawn=0;
 while(a>=0x1000&&a<0x2000&&n<128){int duplicate=0;for(unsigned k=0;k<n;k++)if(active[k]==a)duplicate=1;if(duplicate)break;active[n++]=a;a=word(a+4);}
 // The game buckets actors by Y, prepending equal-depth entries; its two
 // ordering flags place effects before or behind ordinary actors.
 for(unsigned k=0;k<n/2;k++){unsigned t=active[k];active[k]=active[n-1-k];active[n-1-k]=t;}
 for(unsigned k=1;k<n;k++){unsigned value=active[k],j=k;int f=word(value+16),rank=(f&2)?65536:(f&1)?-1:word(value+22);
  while(j){unsigned prev=active[j-1];int pf=word(prev+16),pr=(pf&2)?65536:(pf&1)?-1:word(prev+22);if(pr>=rank)break;active[j]=prev;j--;}active[j]=value;
 }
 for(unsigned index=0;index<n;index++){
  a=active[index];unsigned flags=word(a+16),bank=actor_frame[0x10000+a+8],group=word(0x10000+a+12),flip=word(a+14);
  unsigned source_bank=0,source_base=0,palette_override=0;
  if((flags&0x80)&&!(flags&0x10)&&(word(0x10000+a+0x28)&1))palette_override=0xe00;
  int dynamic=(flags&0x8000)!=0;
  if(dynamic){
   unsigned index=(word(IOG_STREAM_FLAGS)&0x8000)?word(0x10000+a+0xe):word(0xad4)*6;
   if(index>0x600)continue;
   source_base=read_word(0x81,IOG_STREAM_TABLE+index);source_bank=read_byte(0x81,IOG_STREAM_TABLE+index+2);
  }
  if(!(flags&0x2000)&&group){
   unsigned count=read_byte(bank,group+8);
   if(count>0&&count<64){
    int bx=(int)word(a+20)-word(a+24)-camera_x;
    int by=(int)word(a+22)-word(a+26)-1-camera_y;
    for(unsigned k=0;k<count;k++){
     unsigned part=group+9+k*7,prop=(read_word(bank,part+5)^flip)|palette_override;
     int size=(read_byte(bank,part)&1)?16:8;
     int x=bx+read_byte(bank,part+((flip&0x4000)?2:1));
     int y=by+read_byte(bank,part+((flip&0x8000)?4:3));
     if(x+size<=0||x>=w||y+size<=0||y>=h)continue;
     int priority=(int[]){2,3,6,9}[(prop>>12)&3];
     unsigned base=(prop&256)?PPU_objTileAdr2(g_ppu):PPU_objTileAdr1(g_ppu);
     for(int yy=0;yy<size;yy++)for(int xx=0;xx<size;xx++){
      int dx=x+xx,dy=y+yy;if(dx<0||dy<0||dx>=w||dy>=h||camera_x+dx<0||camera_y+dy<0||camera_x+dx>=word(0x692)||camera_y+dy>=word(0x696))continue;
      int sy=(prop&0x8000)?size-1-yy:yy,sx=(prop&0x4000)?size-1-xx:xx;
      unsigned tile=fp_obj_tile(prop,sx,sy)&255;
      int pix=dynamic?source_pixel(source_bank,source_base,(prop&511)+(sx>>3)+(sy>>3)*16,sx&7,sy&7):char_pixel((base+tile*16)&32767,sx&7,sy&7);
      unsigned at=dy*w+dx;
      if(pix){
       const Raster*r=row_state(dy-top);int color=128+((prop>>9)&7)*16+pix;
       if(!audit_reconstruct&&dx>=left&&dx<left+256&&dy>=top&&dy<top+224)continue;
       if(!fp_claim_obj(obj_claimed,at))continue;
       int win=window(r,4,dx-left);
       if((r->main&16)&&(!(r->mainwin&16)||!win)&&priority>=depth[at]){colors[at]=color;sources[at]=((prop>>9)&7)<4?6:4;depth[at]=priority;}
       if((r->sub&16)&&(!(r->subwin&16)||!win)&&priority>=subdepth[at]){subcolors[at]=color;subdepth[at]=priority;}
      }
     }
     actors_drawn++;
    }
   }
  }
 }
}
void IogPortraitSetLayout(float z,int f,int edge,int pad){atomic_store(&zoom,z);atomic_store(&filter,f);atomic_store(&safe_edge,edge);atomic_store(&pad_mode,pad);}
int IogPortraitFilter(void){return atomic_load(&filter);}
void IogPortraitCapture(const uint8_t *field){(void)field;}
const uint32_t *IogPortraitCompose(const uint32_t*native,int nw,int nh,int dw,int dh,int*ow,int*oh){
 if(!g_snes||!g_ppu||nw!=256||nh!=224||dw<=0||dh<=0)return NULL;
 /* Auto-fit uses whole physical pixels. The extra columns are real world
    pixels, avoiding the 4/5-pixel cadence of nearest-neighbour 1080/256. */
 float scale=IogPortraitDrawScale(dw,dh);
 int w=(int)ceilf(dw/scale),h=(int)ceilf(dh/scale);
 if(w<128)w=128;if(w>MAX_W)w=MAX_W;if(h<224)h=224;if(h>MAX_H)h=MAX_H;
 int left=(w-256)/2,top=(h-224)/2;
 int map=word(0x644),play=word(IOG_PLAYER_ACTOR)!=0;
 int world=play&&map!=0xff&&PPU_mode(g_ppu)==1&&word(0x692)>=256&&word(0x696)>=256;
 int good2=world&&raster_valid&&load_set(1);
 if(world && raster_valid && load_set(0)&&map_ready(good2)){
 int cx=word(0x68a)-left,cy=word(0x68e)-top;
  int bounds_w=word(0x692),bounds_h=word(0x696);
  memset(depth,0,(size_t)w*h);memset(subdepth,0,(size_t)w*h);
  memset(colors,0,(size_t)w*h);memset(subcolors,0,(size_t)w*h);memset(sources,5,(size_t)w*h);
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   /* The native field is copied below; draw only the added world. */
   if(!audit_reconstruct&&x>=left&&x<left+256&&y>=top&&y<top+224)continue;
   int at=y*w+x;const Raster*r=row_state(y-top);
   for(int layer=1;layer>=0;layer--){
    if(layer&&!good2)continue;
    int sx=unwrap(r->sx[layer],word(0x68a+2*layer))-left+x;
    /* Visible PPU rows are 1..224; output rows are 0..223. */
    int sy=unwrap(r->sy[layer],word(0x68e + 2*layer))-top+y+1;
    int p=0,c=bg_sample(layer,sx,sy,&p,r);if(!c)continue;
    int win=window(r,layer,x-left);
    if((r->main&(1<<layer))&&(!(r->mainwin&(1<<layer))||!win)&&p>depth[at]){colors[at]=c;sources[at]=layer;depth[at]=p;}
    if((r->sub&(1<<layer))&&(!(r->subwin&(1<<layer))||!win)&&p>subdepth[at]){subcolors[at]=c;subdepth[at]=p;}
   }
  }
  memset(obj_claimed,0,(size_t)w*h);
  int actor_cx=unwrap(raster[112].sx[0],word(0x68a))-left,actor_cy=unwrap(raster[112].sy[0],word(0x68e))-top;
  reading_actors=1;actor_parts(w,h,actor_cx,actor_cy,left,top);reading_actors=0;
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   if(!audit_reconstruct&&x>=left&&x<left+256&&y>=top&&y<top+224)continue;
   int at=y*w+x,wx=cx+x,wy=cy+y;
   image[at]=(wx<0||wy<0||wx>=bounds_w||wy>=bounds_h)?0:shade(row_state(y-top),x-left,colors[at],sources[at],subcolors[at],subdepth[at]!=0);
  }
  /* Preserve the precise native raster (HUD, dialogue, actor animation and
     scanline effects), while the surrounding world remains live. */
  if(!audit_reconstruct)for(int y=0;y<224;y++){int dy=top+y;if(dy<0||dy>=h)continue;for(int x=0;x<256;x++){int dx=left+x;if(dx>=0&&dx<w)image[dy*w+dx]=native[y*256+x]&0xffffff;}}
  memcpy(saved,image,(size_t)w*h*4);saved_w=w;saved_h=h;world_map=map;
 }else{
  if(play&&map==0xff&&saved_w==w&&saved_h==h)memcpy(image,saved,(size_t)w*h*4);
  else memset(image,0,(size_t)w*h*4);
  if(!audit_reconstruct)for(int y=0;y<224;y++){int dy=top+y;if(dy<0||dy>=h)continue;for(int x=0;x<256;x++){int dx=left+x;if(dx>=0&&dx<w)image[dy*w+dx]=native[y*256+x]&0xffffff;}}
 }
  if(hud_active){
   int offset=0;
   for(int y=0;y<40;y++)for(int x=0;x<256;x++){
    int dx=left+x,dy=offset+y;
    if(dx>=0&&dx<w&&dy>=0&&dy<h&&(hud_pixels[y*256+x]>>24))image[dy*w+dx]=hud_pixels[y*256+x]&0xffffff;
   }
  }
 /* The physical edge is fixed. The two corner tips follow the same 200/160
    geometry as the recent FlipPad ports, with a 50px centre clearance. */
 if(atomic_load(&pad_mode)){
  float edge=atomic_load(&safe_edge),radius=200.0f*dw/1080,tangent=160.0f*dw/1080;
  for(int x=0;x<w;x++){
   float sx=(x+.5f)*dw/w,d=fmaxf(0,tangent-fminf(sx,dw-sx));
   float ey=edge+radius-sqrtf(fmaxf(0,radius*radius-d*d));
   int y0=(int)ceilf(ey*h/dh);if(y0<0)y0=0;for(int y=y0;y<h;y++)image[y*w+x]=0;
  }
 }
 const char *dump=getenv("IOG_PORTRAIT_DUMP");static int dumped;
 if(dump&&!dumped&&world){FILE*f=fopen(dump,"wb");if(f){fprintf(f,"P6\n%d %d\n255\n",w,h);for(int i=0;i<w*h;i++){fputc(image[i]>>16,f);fputc(image[i]>>8,f);fputc(image[i],f);}fclose(f);dumped=1;fprintf(stderr,"[portrait] map=%d %dx%d actors=%u\n",map,w,h,actors_drawn);}}
 *ow=w;*oh=h;return image;
}
void IogPortraitBegin(void){
 if(!g_ppu||!g_snes)return;
 PpuClearOverlayCaptures(g_ppu);hud_active=0;
 int play=word(IOG_PLAYER_ACTOR)!=0,map=word(0x644);
 const uint16_t*row=PpuRenderVram(g_ppu)+(PPU_bgTilemapAdr(g_ppu,2)&32767)+32;
 if(play&&map!=0xff&&(row[11]&1023)==0xef&&(row[20]&1023)==0xef&&(row[15]&1023)==0x1e){
  g_ppu->overlayRenderBuffer[kPpuOverlaySource_Bg3]=(uint8_t*)hud_pixels;
  g_ppu->overlayRenderPitch[kPpuOverlaySource_Bg3]=256*4;
  PpuSetOverlayCapture(g_ppu,kPpuOverlaySource_Bg3,0,0,256,40,kPpuOverlayFlag_RemoveFromGame);hud_active=1;
 }
}

float IogPortraitDrawScale(int w,int h){
 float z=atomic_load(&zoom);if(z<=0)z=fmaxf(1,floorf(w/256.0f));
 if(w>h)z=fminf(fmaxf(z,ceilf(w/(float)MAX_W)),fmaxf(1,floorf(h/224.0f)));
 return z;
}

/* Development-only reconstruction audit. Repaint the native field through the
 * same path as the added world, so retaining native pixels cannot hide errors. */
void IogPortraitAudit(const uint8_t*field,unsigned frame){
 const char*folder=getenv("IOG_RENDER_AUDIT_DIR");if(!folder||frame%30)return;
 int w,h;char path[2048];
 for(int pass=0;pass<3;pass++){
  audit_reconstruct=pass==1;const uint32_t*p=(const uint32_t*)field;
  if(pass<2)p=IogPortraitCompose(p,256,224,1080,1664,&w,&h);else{w=256;h=224;}
  if(!p)continue;snprintf(path,sizeof(path),"%s/%s-%u.ppm",folder,pass==0?"expanded":pass==1?"reconstructed":"native",frame);FILE*f=fopen(path,"wb");if(!f)continue;
  fprintf(f,"P6\n%d %d\n255\n",w,h);for(int i=0;i<w*h;i++){fputc(p[i]>>16,f);fputc(p[i]>>8,f);fputc(p[i],f);}fclose(f);
 }
 audit_reconstruct=0;
}
/* Latch display-only actor data before raster IRQs run. */
void IogPortraitResetFrame(void){raster_valid=0;if(g_snes)memcpy(actor_frame,g_snes->ram,sizeof(actor_frame));}
