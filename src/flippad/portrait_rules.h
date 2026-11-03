/* Shared presentation rules used by the three FlipPad game hosts.
 * They inspect display data only; no game state or actor scripts are changed.
 * Repository and dependency notices continue to apply. */
#ifndef FLIPPAD_PORTRAIT_RULES_H
#define FLIPPAD_PORTRAIT_RULES_H
#include <stdint.h>
#include <stddef.h>
static inline int fp_supported_background(int mode,int large_tiles,int mosaic){return mode==1&&!large_tiles&&!mosaic;}
static inline int fp_unwrap10(int actual,int camera){return camera+((actual-camera+512)&1023)-512;}
static inline unsigned fp_obj_tile(unsigned prop,int x,int y){
 return (prop&256)|(((prop&0xf0)+(unsigned)(y>>3)*16)&0xf0)|(((prop&15)+(unsigned)(x>>3))&15);
}
static inline int fp_window(uint32_t select,uint16_t logic,const uint8_t left[2],const uint8_t right[2],int layer,int x){
 unsigned f=(select>>(layer*4))&15;
 int a=(x>=left[0]&&x<=right[0])^(f&1),b=(x>=left[1]&&x<=right[1])^((f>>2)&1);
 if(!(f&2))return(f&8)?b:0;
 if(!(f&8))return a;
 switch((logic>>(layer*2))&3){case 0:return a||b;case 1:return a&&b;case 2:return a!=b;default:return a==b;}
}
/* OBJ/OBJ selection happens in OAM order before OBJ/BG priority comparison.
 * An earlier opaque OBJ blocks a later one even when BG hides the earlier OBJ. */
static inline int fp_claim_obj(uint8_t *claimed,size_t pixel){
 if(claimed[pixel])return 0;
 claimed[pixel]=1;return 1;
}
#endif
