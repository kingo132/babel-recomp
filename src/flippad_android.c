/* Android suspension: serialize only on the emulation thread at a frame boundary.
 * The suspend file is separate from every numbered manual save slot. */
#include <jni.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include <SDL3/SDL.h>
#include "common_rtl.h"
static atomic_bool ready, requested, completed;
int FlipPadSuspendPoll(void) {
    if (!atomic_exchange(&requested, false)) return 0;
    RtlEnsureSaveDir();
    char temp[1024], path[1024];
    snprintf(path, sizeof(path), "%s/suspend.sav", RtlSaveRoot());
    snprintf(temp, sizeof(temp), "%s/suspend.tmp", RtlSaveRoot());
    if (RtlSaveSnapshot(temp)) {
        FILE *f = fopen(temp, "rb");
        if (f) { fsync(fileno(f)); fclose(f); }
        if (rename(temp, path) == 0) fprintf(stderr, "[FlipPad] suspend saved\n");
    }
    RtlWriteSram();
    atomic_store(&completed, true);
    return 1;
}
int FlipPadResume(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/suspend.sav", RtlSaveRoot());
    int restored = RtlLoadSnapshot(path);
    atomic_store(&ready, true);
    fprintf(stderr, "[FlipPad] suspend restored=%d\n", restored);
    return restored;
}
void FlipPadSuspendStop(void) {
    if (atomic_exchange(&ready, false)) {
        atomic_store(&requested, true);
        FlipPadSuspendPoll();
    }
}
JNIEXPORT void JNICALL Java_com_mr0ta_babelrecomp_BabelActivity_nativeSuspend(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (!atomic_load(&ready)) return;
    atomic_store(&completed, false);
    atomic_store(&requested, true);
    /* Before SDLActivity pauses its native thread, give that thread a bounded
       opportunity to save. Never read or modify guest state on the UI thread. */
    for (int i = 0; i < 200 && !atomic_load(&completed); i++) SDL_Delay(5);
}
