#include <jni.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "common_rtl.h"
#include "iog_portrait.h"
#include "snes_savestate_menu.h"
#include "snes/snes.h"
extern void snesrecomp_desktop_set_speed(double speed);
extern void snesrecomp_desktop_set_paused(int pause);
extern void IogCheatSet(int which,int on);
static atomic_bool ready,requested,completed,user_paused;
static atomic_int buttons,command,argument,result,pulse,pulse_frames;
static _Atomic float game_speed=2;
int FlipPadUserPaused(void){return atomic_load(&user_paused);}
uint32_t FlipPadButtons(void){
 uint32_t b=atomic_load(&buttons);
 int f=atomic_load(&pulse_frames);
 if(f>0){b|=atomic_load(&pulse);atomic_store(&pulse_frames,f-1);}return b;
}
static void save_suspend(void){
 RtlEnsureSaveDir();char temp[1024],path[1024];snprintf(path,sizeof(path),"%s/suspend.sav",RtlSaveRoot());snprintf(temp,sizeof(temp),"%s/suspend.tmp",RtlSaveRoot());
 if(RtlSaveSnapshot(temp)){FILE*f=fopen(temp,"rb");if(f){fsync(fileno(f));fclose(f);}if(rename(temp,path)==0)fprintf(stderr,"[FlipPad] suspend saved\n");}RtlWriteSram();
}
int FlipPadSuspendPoll(void){
 static float applied_speed;
 float speed=atomic_load(&game_speed);
 if(speed!=applied_speed){snesrecomp_desktop_set_speed(speed);applied_speed=speed;}
 snesrecomp_desktop_set_paused(atomic_load(&user_paused));
 int cmd=atomic_exchange(&command,0);
 if(cmd){int arg=atomic_load(&argument),ok=0;
  if(cmd==1)ok=snes_savestate_save_slot(arg);
  if(cmd==2)ok=snes_savestate_load_slot(arg);
  if(cmd==3){IogCheatSet(arg>>1,arg&1);ok=1;}
  if(cmd==4){RtlReset(1);ok=1;}
  atomic_store(&result,ok?1:-1);
 }
 if(!atomic_exchange(&requested,false))return 0;
 save_suspend();atomic_store(&completed,true);return 1;
}
int FlipPadResume(void){char path[1024];snprintf(path,sizeof(path),"%s/suspend.sav",RtlSaveRoot());int restored=RtlLoadSnapshot(path);atomic_store(&ready,true);fprintf(stderr,"[FlipPad] suspend restored=%d\n",restored);return restored;}
void FlipPadSuspendStop(void){if(atomic_exchange(&ready,false))save_suspend();}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativeSuspend(JNIEnv*e,jclass c){(void)e;(void)c;if(!atomic_load(&ready))return;atomic_store(&completed,false);atomic_store(&requested,true);for(int i=0;i<200&&!atomic_load(&completed);i++)SDL_Delay(5);}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativePad(JNIEnv*e,jclass c,jint mask){(void)e;(void)c;atomic_store(&buttons,mask);}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativePauseGame(JNIEnv*e,jclass c,jboolean pause){(void)e;(void)c;atomic_store(&buttons,0);atomic_store(&user_paused,pause);}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativeSettings(JNIEnv*e,jclass c,jfloat speed,jfloat zoom,jint filter,jint edge,jboolean pad){(void)e;(void)c;atomic_store(&game_speed,speed);IogPortraitSetLayout(zoom,filter,edge,pad);}
JNIEXPORT jint JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativeAction(JNIEnv*e,jclass c,jint cmd,jint arg){(void)e;(void)c;if(!atomic_load(&ready))return -1;atomic_store(&result,0);atomic_store(&argument,arg);atomic_store(&command,cmd);for(int i=0;i<300&&!atomic_load(&result);i++)SDL_Delay(5);return atomic_load(&result);}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativePulse(JNIEnv*e,jclass c,jint mask){(void)e;(void)c;atomic_store(&pulse,mask);atomic_store(&pulse_frames,3);}
