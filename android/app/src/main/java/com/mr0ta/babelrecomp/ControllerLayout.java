package com.mr0ta.babelrecomp;

/** Controller identity and geometry policy; independent of device display state. */
final class ControllerLayout {
    static final int TOUCH=0, GAMEPAD=1, FLIPPAD=2;
    static int classify(boolean virtual,boolean gamepad,int vendor,int product){
        if(virtual||!gamepad)return TOUCH;
        return vendor==0x2dc8&&product==0x3022?FLIPPAD:GAMEPAD;
    }
    static float landscapeScale(float width,float height,float scale,boolean integer){
        if(width<=height)return scale;
        float limit=Math.max(1,height/224f);
        float minimum=width/640f;
        if(integer){limit=Math.max(1,(float)Math.floor(limit));minimum=(float)Math.ceil(minimum);}
        return Math.min(limit,Math.max(minimum,scale));
    }
}
