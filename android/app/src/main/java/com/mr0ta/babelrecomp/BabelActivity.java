package com.mr0ta.babelrecomp;
import android.app.AlertDialog;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.hardware.input.InputManager;
import android.os.*;
import android.util.Log;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import org.libsdl.app.SDLActivity;

public class BabelActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static final String TAG="GaiaFlipPad";
    private static final String[] BUNDLED={"assets","i18n","mods"};
    private static native void nativeSuspend();
    private static native void nativePad(int mask);
    private static native void nativePauseGame(boolean pause);
    private static native void nativeSettings(float speed,float zoom,int filter,int edge,boolean pad);
    private static native int nativeAction(int command,int value);
    private static native void nativePulse(int mask);
    FrameLayout root; LinearLayout footer,card,content; TextView status,notice;
    android.content.SharedPreferences prefs;
    final Handler ui=new Handler(Looper.getMainLooper());
    InputManager inputs;
    boolean flipPad,controller,assistant,hatUp,hatDown; int tab,held,hat,lastMenuKey=-1,lastHat;
    float speed=2,zoom; int filter; boolean[] cheats=new boolean[3];
    boolean[] shoulders=new boolean[4];
    final float[] speeds={.5f,1,1.5f,2,3,4};
    final String[] filterNames={"原始像素","平滑","xBRZ 2×","xBRZ 3×"};
    @Override protected String[] getLibraries(){return new String[]{"main"};}
    @Override protected String[] getArguments(){return new String[]{"--no-launcher",new File(getExternalFilesDir(null),"rom.sfc").getAbsolutePath()};}
    @Override protected void onCreate(Bundle state){
        installBundledFiles();super.onCreate(state);if(mBrokenLibraries||mLayout==null)return;
        prefs=getSharedPreferences("flippad",0);speed=prefs.getFloat("speed",2);zoom=prefs.getFloat("zoom",0);if(zoom>0)zoom=Math.max(2,Math.min(6,Math.round(zoom)));filter=prefs.getInt("filter",0);
        for(int i=0;i<3;i++)cheats[i]=prefs.getBoolean("cheat-"+i,false);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if(Build.VERSION.SDK_INT>=28)getWindow().getAttributes().layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        if(Build.VERSION.SDK_INT>=30)getWindow().setDecorFitsSystemWindows(false);
        getWindow().setStatusBarColor(Color.TRANSPARENT);getWindow().setNavigationBarColor(Color.BLACK);
        if(Build.VERSION.SDK_INT>=26)mSurface.setDefaultFocusHighlightEnabled(false);
        mSurface.setStateListAnimator(null);setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        ViewGroup parent=(ViewGroup)mLayout.getParent();if(parent!=null)parent.removeView(mLayout);
        root=new FrameLayout(this);root.setBackgroundColor(Color.BLACK);root.addView(mLayout,new FrameLayout.LayoutParams(-1,-1));setContentView(root);
        root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->layoutGame());
        root.setOnApplyWindowInsetsListener((v,insets)->{root.post(this::layoutGame);return insets;});
        notice=new TextView(this);notice.setTextColor(Color.WHITE);notice.setTextSize(13);notice.setPadding(dp(10),dp(7),dp(10),dp(7));notice.setBackground(bg(0xe6203943));notice.setVisibility(View.GONE);root.addView(notice);
        inputs=(InputManager)getSystemService(INPUT_SERVICE);inputs.registerInputDeviceListener(this,ui);scanControllers();root.post(this::layoutGame);immersive();
    }
    void immersive(){getWindow().getDecorView().setSystemUiVisibility(5894|1024|512);}
    @Override public void onWindowFocusChanged(boolean focus){super.onWindowFocusChanged(focus);if(root==null)return;if(focus)immersive();else clearButtons();}
    @Override public void setOrientationBis(int w,int h,boolean resizable,String hint){setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);}
    @Override protected void onPause(){if(!mBrokenLibraries){clearButtons();nativeSuspend();}super.onPause();}
    @Override protected void onDestroy(){if(inputs!=null)inputs.unregisterInputDeviceListener(this);super.onDestroy();}
    @Override public void onBackPressed(){if(assistant)hideAssistant();else showAssistant();}
    public void onInputDeviceAdded(int id){scanControllers();}
    public void onInputDeviceChanged(int id){scanControllers();}
    public void onInputDeviceRemoved(int id){scanControllers();}
    int dp(float n){return Math.round(n*getResources().getDisplayMetrics().density);}
    int safe(){int w=root.getWidth(),h=root.getHeight();return flipPad?Math.min(h,Math.round(h*2f/3f-50*w/1080f)):Math.max(1,h-(controller?0:dp(62)));}
    int inset(){WindowInsets wi=root.getRootWindowInsets();return wi!=null&&Build.VERSION.SDK_INT>=28&&wi.getDisplayCutout()!=null?wi.getDisplayCutout().getSafeInsetTop():dp(24);}
    void layoutGame(){
        if(root==null||root.getWidth()==0)return;int w=root.getWidth(),h=root.getHeight();
        int surfaceHeight=flipPad?Math.min(h,safe()+Math.round(80*w/1080f)):safe();
        FrameLayout.LayoutParams p=(FrameLayout.LayoutParams)mLayout.getLayoutParams();if(p.height!=surfaceHeight||p.topMargin!=0){p.height=surfaceHeight;p.topMargin=0;mLayout.setLayoutParams(p);}
        if(footer!=null){FrameLayout.LayoutParams f=new FrameLayout.LayoutParams(-1,dp(62),Gravity.BOTTOM);footer.setLayoutParams(f);}
        nativeSettings(speed,zoom,filter,safe(),flipPad);
        if(notice!=null){FrameLayout.LayoutParams n=new FrameLayout.LayoutParams(-2,-2);n.leftMargin=dp(8);n.topMargin=Math.max(inset()+dp(3),dp(25));notice.setLayoutParams(n);notice.bringToFront();}
        if(card!=null){FrameLayout.LayoutParams c=new FrameLayout.LayoutParams(w-dp(16),Math.max(dp(280),safe()-inset()-dp(14)));c.leftMargin=dp(8);c.topMargin=inset()+dp(6);card.setLayoutParams(c);card.bringToFront();}
    }
    void scanControllers(){
        flipPad=false;controller=false;
        for(int id:InputDevice.getDeviceIds()){InputDevice d=InputDevice.getDevice(id);if(d==null||d.isVirtual())continue;int s=d.getSources();boolean pad=(s&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD||(s&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK;
            boolean flip=d.getVendorId()==0x2dc8&&d.getProductId()==0x3022||d.getName().toLowerCase(Locale.ROOT).contains("flippad");flipPad|=flip;controller|=pad||flip;}
        rebuildFooter();
    }
    void rebuildFooter(){
        clearButtons();if(footer!=null)root.removeView(footer);footer=null;
        if(!controller){footer=new LinearLayout(this);footer.setPadding(dp(2),dp(3),dp(2),dp(3));footer.setBackgroundColor(0xff14232f);
            String[] names={"←","↑","↓","→","Y","X","B","A","开始","助手"};int[] masks={64,16,32,128,2,512,1,256,8,0};
            for(int i=0;i<names.length;i++){int mask=masks[i];Button b=new Button(this);b.setText(names[i]);b.setTextColor(Color.WHITE);b.setTextSize(i<8?14:10);b.setPadding(0,0,0,0);b.setMinWidth(0);b.setMinimumWidth(0);b.setAllCaps(false);b.setFocusable(false);b.setBackground(bg(0xff293b4c));LinearLayout.LayoutParams bp=new LinearLayout.LayoutParams(0,-1,1);bp.setMargins(dp(1),0,dp(1),0);footer.addView(b,bp);
                b.setOnTouchListener((v,e)->{int a=e.getActionMasked();if(mask==0){if(a==MotionEvent.ACTION_UP)showAssistant();return true;}if(a==MotionEvent.ACTION_DOWN){held|=mask;v.setPressed(true);}else if(a==MotionEvent.ACTION_UP||a==MotionEvent.ACTION_CANCEL){held&=~mask;v.setPressed(false);}nativePad(assistant?0:held|hat);return true;});}
            root.addView(footer,new FrameLayout.LayoutParams(-1,dp(62),Gravity.BOTTOM));}
        layoutGame();if(card!=null)card.bringToFront();
    }
    void clearButtons(){held=hat=0;lastHat=0;Arrays.fill(shoulders,false);if(!mBrokenLibraries)nativePad(0);}
    GradientDrawable bg(int color){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(7));return d;}
    void style(Button b,boolean selected){b.setAllCaps(false);b.setTextSize(13);b.setTextColor(selected?0xff14242d:Color.WHITE);b.setPadding(dp(2),0,dp(2),0);b.setMinWidth(0);b.setMinimumWidth(0);StateListDrawable states=new StateListDrawable();GradientDrawable focused=bg(selected?0xff8ee4cd:0xff294456);focused.setStroke(dp(2),0xffaaf2dd);states.addState(new int[]{android.R.attr.state_focused},focused);states.addState(new int[]{android.R.attr.state_pressed},bg(0xff4a716f));states.addState(new int[]{},bg(selected?0xff8ee4cd:0xff293b4c));b.setBackground(states);}
    void feedback(String text){if(status!=null&&assistant){status.setText(text);status.announceForAccessibility(text);}else if(notice!=null){notice.setText(text);notice.setVisibility(View.VISIBLE);notice.removeCallbacks(hideNotice);notice.postDelayed(hideNotice,2400);}root.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);}
    final Runnable hideNotice=()->{if(notice!=null)notice.setVisibility(View.GONE);};
    void remember(){android.content.SharedPreferences.Editor e=prefs.edit().putFloat("speed",speed).putFloat("zoom",zoom).putInt("filter",filter);for(int i=0;i<3;i++)e.putBoolean("cheat-"+i,cheats[i]);e.apply();layoutGame();}
    Button action(LinearLayout row,String title,Runnable run){Button b=new Button(this);b.setText(title);style(b,false);LinearLayout.LayoutParams bp=new LinearLayout.LayoutParams(0,dp(40),1);bp.setMargins(dp(2),dp(2),dp(2),dp(2));row.addView(b,bp);b.setOnClickListener(v->{v.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);run.run();});return b;}
    LinearLayout row(){LinearLayout r=new LinearLayout(this);content.addView(r,new LinearLayout.LayoutParams(-1,-2));return r;}
    void label(String text){TextView t=new TextView(this);t.setText(text);t.setTextColor(0xffcbd9e5);t.setTextSize(13);t.setPadding(dp(4),dp(5),dp(4),dp(4));content.addView(t);}
    void showAssistant(){if(root==null)return;clearButtons();nativePauseGame(true);assistant=true;renderAssistant();}
    void hideAssistant(){if(card!=null)root.removeView(card);card=null;status=null;assistant=false;clearButtons();nativePauseGame(false);mSurface.requestFocus();immersive();}
    void renderAssistant(){
        if(card!=null)root.removeView(card);card=new LinearLayout(this);card.setOrientation(1);card.setPadding(dp(8),dp(7),dp(8),dp(7));card.setBackground(bg(0xf51a2937));root.addView(card);
        TextView title=new TextView(this);title.setText("游戏助手");title.setTextSize(19);title.setTextColor(Color.WHITE);title.setPadding(dp(4),0,0,dp(4));card.addView(title);
        LinearLayout tabs=new LinearLayout(this);card.addView(tabs);String[] names={"存档","速度","画面","辅助"};
        for(int i=0;i<4;i++){final int n=i;Button b=new Button(this);b.setText(names[i]);style(b,i==tab);LinearLayout.LayoutParams q=new LinearLayout.LayoutParams(0,dp(39),1);q.setMargins(dp(2),0,dp(2),dp(3));tabs.addView(b,q);b.setOnClickListener(v->{tab=n;renderAssistant();});}
        content=new LinearLayout(this);content.setOrientation(1);card.addView(content,new LinearLayout.LayoutParams(-1,0,1));
        if(tab==0)buildSaves();if(tab==1)buildSpeed();if(tab==2)buildDisplay();if(tab==3)buildTools();
        status=new TextView(this);status.setTextColor(0xffaaf2dd);status.setTextSize(12);status.setGravity(Gravity.CENTER_VERTICAL);status.setText("方向键 / A 操作，B 返回，肩键切换分类");card.addView(status,new LinearLayout.LayoutParams(-1,dp(30)));
        Button back=new Button(this);back.setText("继续游戏");style(back,true);card.addView(back,new LinearLayout.LayoutParams(-1,dp(40)));back.setOnClickListener(v->hideAssistant());layoutGame();back.requestFocus();
    }
    void buildSaves(){
        label("6 个独立手动槽 · 后台续玩单独保存");
        for(int y=0;y<3;y++){LinearLayout r=new LinearLayout(this);content.addView(r,new LinearLayout.LayoutParams(-1,0,1));for(int x=0;x<2;x++){
            int slot=y*2+x;LinearLayout cell=new LinearLayout(this);cell.setOrientation(1);cell.setPadding(dp(4),dp(3),dp(4),dp(3));LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(0,-1,1);cp.setMargins(dp(3),dp(3),dp(3),dp(3));r.addView(cell,cp);cell.setBackground(bg(0xff223444));TextView meta=new TextView(this);meta.setTextColor(Color.WHITE);meta.setTextSize(12);meta.setText(slotText(slot));cell.addView(meta,new LinearLayout.LayoutParams(-1,0,1));LinearLayout buttons=new LinearLayout(this);cell.addView(buttons);action(buttons,"保存",()->{int ok=nativeAction(1,slot);meta.setText(slotText(slot));feedback(ok==1?"已保存到 "+(slot+1)+" 号槽":"保存失败，请稍后重试");});action(buttons,"读取",()->{if(!slotFile(slot).exists()){feedback("这个存档槽是空的");return;}int ok=nativeAction(2,slot);if(ok==1){hideAssistant();feedback("已读取 "+(slot+1)+" 号槽");}else feedback("读取失败，原进度未替换");});}}
    }
    File slotFile(int n){return new File(getExternalFilesDir(null),"saves/save"+n+".sav");}
    String slotText(int n){File f=slotFile(n);return (n+1)+" 号槽  "+(f.exists()?new java.text.SimpleDateFormat("MM-dd HH:mm",Locale.ROOT).format(new Date(f.lastModified())):"空槽");}
    void buildSpeed(){label("游戏速度 "+speed+"×");LinearLayout r=row();for(float v:speeds)action(r,v+"×",()->{speed=v;remember();renderAssistant();feedback("游戏 "+speed+"× · 音乐保持 1×");});label("音乐与音效保持正常播放速率");label("L1 减速 · L2 加速，设置会自动记住");}
    void buildDisplay(){
        label("地图视野："+(zoom==0?"整数适配 / 满宽":zoom+"× 等比缩放"));LinearLayout r=row();action(r,"整数适配",()->setZoom(0));for(float z:new float[]{2,3,4,5})action(r,((int)z)+"×",()->setZoom(z));
        SeekBar seek=new SeekBar(this);seek.setMax(4);seek.setProgress(Math.round((zoom==0?(float)Math.floor(root.getWidth()/256f):zoom)-2));content.addView(seek,new LinearLayout.LayoutParams(-1,dp(38)));seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){feedback("视野 "+zoom+"×");}public void onProgressChanged(SeekBar b,int v,boolean user){if(user){zoom=2+v;remember();}}});
        label("滤镜："+filterNames[filter]);LinearLayout f=row();for(int i=0;i<4;i++){int k=i;Button b=action(f,filterNames[i],()->{filter=k;remember();renderAssistant();feedback("滤镜："+filterNames[k]);});style(b,i==filter);}
        label("地图自动扩展；插拔手柄自动切换布局");
    }
    void setZoom(float z){zoom=z;remember();renderAssistant();feedback(z==0?"整数适配 / 满宽":"视野 "+z+"×");}
    void buildTools(){
        for(int i=0;i<3;i++){int n=i;CheckBox b=new CheckBox(this);b.setText(new String[]{"生命保护","暗黑力量 DP 99","能力增强"}[i]);b.setTextColor(Color.WHITE);b.setTextSize(14);b.setChecked(cheats[i]);content.addView(b,new LinearLayout.LayoutParams(-1,dp(37)));b.setOnCheckedChangeListener((v,on)->{if(nativeAction(3,(n<<1)|(on?1:0))==1){cheats[n]=on;remember();feedback(on?"已开启":"已关闭");}});}
        LinearLayout menu=row();action(menu,"物品 / 装备",()->{hideAssistant();nativePulse(4);});
        LinearLayout shoulder=row();action(shoulder,"原版 L",()->{hideAssistant();nativePulse(1024);});action(shoulder,"原版 R",()->{hideAssistant();nativePulse(2048);});
        LinearLayout reset=row();action(reset,"重新开始",()->new AlertDialog.Builder(this).setMessage("返回游戏标题？手动存档会保留。").setPositiveButton("返回标题",(d,w)->{nativeAction(4,0);hideAssistant();}).setNegativeButton("取消",null).show());
    }
    void changeSpeed(int delta){int nearest=0;float best=Float.MAX_VALUE;for(int i=0;i<speeds.length;i++)if(Math.abs(speeds[i]-speed)<best){nearest=i;best=Math.abs(speeds[i]-speed);}speed=speeds[Math.max(0,Math.min(speeds.length-1,nearest+delta))];remember();feedback("游戏 "+speed+"× · 音乐 1×");}
    void shortcut(int key,boolean down){int index=key-102;if(index<0||index>=4)return;if(shoulders[index]==down)return;shoulders[index]=down;if(!down)return;
        if(assistant){tab=(tab+(key==102||key==104?3:1))%4;renderAssistant();return;}
        if(key==102||key==104)changeSpeed(key==102?-1:1);
        if(key==103){float[] z={0,3,4,5};int i=0;for(int k=0;k<z.length;k++)if(zoom==z[k])i=k;zoom=z[(i+1)%z.length];remember();feedback(zoom==0?"整数适配 / 满宽":"视野 "+zoom+"×");}
        if(key==105){filter=(filter+1)%4;remember();feedback("滤镜："+filterNames[filter]);}
    }
    @Override public boolean dispatchKeyEvent(KeyEvent e){
        if(root==null||mBrokenLibraries)return super.dispatchKeyEvent(e);int k=e.getKeyCode();boolean down=e.getAction()==KeyEvent.ACTION_DOWN;
        if(k==109||k==4){if(down&&e.getRepeatCount()==0){if(assistant)hideAssistant();else showAssistant();}return true;}
        if(k>=102&&k<=105){shortcut(k,down);return true;}
        if(assistant){if(k==97){if(down&&e.getRepeatCount()==0)hideAssistant();return true;}if(k==96)return super.dispatchKeyEvent(new KeyEvent(e.getDownTime(),e.getEventTime(),e.getAction(),23,e.getRepeatCount()));return super.dispatchKeyEvent(e);}
        int mask=k==19?16:k==20?32:k==21?64:k==22?128:k==96?256:k==97?1:k==99?512:k==100?2:k==108?8:0;
        if(mask!=0){if(down)held|=mask;else held&=~mask;nativePad(held|hat);return true;}return super.dispatchKeyEvent(e);
    }
    @Override public boolean dispatchGenericMotionEvent(MotionEvent e){
        if(root==null||(e.getSource()&InputDevice.SOURCE_JOYSTICK)==0)return super.dispatchGenericMotionEvent(e);
        float x=e.getAxisValue(MotionEvent.AXIS_HAT_X),y=e.getAxisValue(MotionEvent.AXIS_HAT_Y);if(Math.abs(x)<.3)x=e.getAxisValue(MotionEvent.AXIS_X);if(Math.abs(y)<.3)y=e.getAxisValue(MotionEvent.AXIS_Y);
        int bits=(x<-.5?64:x>.5?128:0)|(y<-.5?16:y>.5?32:0);
        if(assistant){if(bits!=0&&bits!=lastHat){int key=y<-.5?19:y>.5?20:x<-.5?21:22;super.dispatchKeyEvent(new KeyEvent(0,key));super.dispatchKeyEvent(new KeyEvent(1,key));}}
        else{hat=bits;nativePad(held|hat);}lastHat=bits;
        shortcut(104,Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER),e.getAxisValue(MotionEvent.AXIS_BRAKE))>.5f);
        shortcut(105,Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER),e.getAxisValue(MotionEvent.AXIS_GAS))>.5f);return true;
    }
    private void installBundledFiles() {
        File root = getExternalFilesDir(null);
        if (root == null) return;
        try {
            File rom = new File(root, "rom.sfc");
            if (!rom.exists()) copyFile(getAssets(), "private/rom.sfc", rom);
        } catch (IOException e) { Log.w(TAG, "Private ROM not bundled: " + e); }
        try {
            File config = new File(root, "config.ini");
            File layoutMarker = new File(root, ".portrait-v2");
            if (!config.exists() || !layoutMarker.exists()) {
                copyFile(getAssets(), "defaults/config.ini", config);
                writeFile(layoutMarker, "0.2.0");
            }
        } catch (IOException e) { Log.w(TAG, "First-run data: " + e); }
        String version;
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName
                    + "-" + getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (Exception e) {
            version = "unknown";
        }
        File marker = new File(root, ".bundled-version");
        if (marker.exists() && version.equals(readFile(marker))) return;
        AssetManager am = getAssets();
        for (String dir : BUNDLED) copyTree(am, dir, new File(root, dir));
        writeFile(marker, version);
    }

    private static void copyTree(AssetManager am, String path, File dst) {
        try {
            String[] children = am.list(path);
            if (children == null || children.length == 0) {
                copyFile(am, path, dst);
                return;
            }
            dst.mkdirs();
            for (String child : children)
                copyTree(am, path + "/" + child, new File(dst, child));
        } catch (IOException e) {
            Log.w(TAG, "copy " + path + ": " + e);
        }
    }

    private static void copyFile(AssetManager am, String path, File dst) throws IOException {
        /* The player's own mod selection is kept across updates. */
        if (dst.getName().equals("state.toml") && dst.exists()) return;
        File parent = dst.getParentFile();
        if (parent != null) parent.mkdirs();
        try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(dst)) {
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        }
    }

    private static String readFile(File f) {
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] b = new byte[256];
            int n = in.read(b);
            return n > 0 ? new String(b, 0, n) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeFile(File f, String s) {
        try (OutputStream out = new FileOutputStream(f)) {
            out.write(s.getBytes());
        } catch (IOException e) {
            Log.w(TAG, "write " + f + ": " + e);
        }
    }
}
