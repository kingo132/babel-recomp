#include "portrait_rules.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 /* Large sprites cross tile-sheet edges without changing the name bank. */
 assert(fp_obj_tile(0xff,8,0)==0xf0);
 assert(fp_obj_tile(0xff,0,8)==0x0f);
 assert(fp_obj_tile(0x1ff,8,8)==0x100);
 assert(fp_obj_tile(0x1a2,15,15)==0x1b3);
 assert(fp_unwrap10(0,1023)==1024);
 assert(fp_unwrap10(1023,0)==-1);
 assert(fp_unwrap10(7,2050)==2055);
 uint8_t l[2]={10,20},r[2]={30,40};
 for(int x=0;x<50;x++){
  int a=x>=10&&x<=30,b=x>=20&&x<=40;
  assert(fp_window(0x0a,0,l,r,0,x)==(a||b));
  assert(fp_window(0x0a,1,l,r,0,x)==(a&&b));
  assert(fp_window(0x0a,2,l,r,0,x)==(a!=b));
  assert(fp_window(0x0a,3,l,r,0,x)==(a==b));
  assert(fp_window(0x03,0,l,r,0,x)==!a);
  assert(fp_window(0x00,0,l,r,0,x)==0);
 }
 uint8_t claim[2]={0};
 int background=5,first_priority=2,later_priority=9,selected=0;
 if(fp_claim_obj(claim,0)&&first_priority>background)selected=1;
 if(fp_claim_obj(claim,0)&&later_priority>background)selected=2;
 assert(selected==0); /* Hidden earlier OBJ still masks the later OBJ. */
 assert(fp_claim_obj(claim,1)==1);
 assert(fp_supported_background(1,0,0));
 assert(!fp_supported_background(7,0,0));
 assert(!fp_supported_background(1,1,0));
 assert(!fp_supported_background(1,0,1));
 puts("portrait rules: tile edges, camera wrap, windows, OBJ overlap, scene eligibility passed");
}
