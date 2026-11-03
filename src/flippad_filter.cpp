#include "flippad/xbrz/xbrz.h"
#include <vector>
#include <cstdint>
extern "C" const uint32_t *FlipPadFilter(const uint32_t *src,int w,int h,int mode,int *ow,int *oh){
 if(mode!=2&&mode!=3){*ow=w;*oh=h;return src;}
 int scale=mode;static std::vector<uint32_t> result;result.resize((size_t)w*h*scale*scale);
 xbrz::scale(scale,src,result.data(),w,h,xbrz::ColorFormat::RGB);
 *ow=w*scale;*oh=h*scale;return result.data();
}
