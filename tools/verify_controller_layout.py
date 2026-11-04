#!/usr/bin/env python3
"""Regression for FlipPad-only layout and landscape native UI visibility."""
from pathlib import Path
import subprocess,tempfile,re
root=Path(__file__).resolve().parents[1]
source=next(p for p in root.rglob('ControllerLayout.java')if '/build/'not in str(p))
package=re.search(r'package ([^;]+);',source.read_text())[1]
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'ControllerLayout.java').write_text(source.read_text())
 (p/'Check.java').write_text('package '+package+';'+r"""
class Check {
 static void check(boolean result,String label){if(!result)throw new AssertionError(label);}
 public static void main(String[] args){
  check(ControllerLayout.classify(false,true,0x358a,0x0304)==1,"actual Backbone must use full-screen controller layout");
  check(ControllerLayout.classify(false,true,0x2dc8,0x3022)==2,"actual FlipPad needs curved layout");
  check(ControllerLayout.classify(false,false,0x2dc8,0x3022)==0,"consumer interface must not enable FlipPad");
  check(ControllerLayout.classify(true,true,0x2dc8,0x3022)==0,"virtual device must not enable FlipPad");
  check(ControllerLayout.classify(false,true,0x054c,0x0ce6)==1,"ordinary USB/Bluetooth controller");
  check(ControllerLayout.classify(false,false,0,0)==0,"no controller keeps touch controls");
  int mode=0;mode=Math.max(mode,ControllerLayout.classify(false,true,0x358a,0x0304));mode=Math.max(mode,ControllerLayout.classify(false,false,0x2dc8,0x3022));check(mode==1,"consumer event must not override Backbone");
  for(float zoom:new float[]{0,2,3,4,5,6,9}){
   float scale=ControllerLayout.landscapeScale(2424,1080,zoom==0?9:zoom,true);
   check(scale*224<=1080,"entire native menu and dialogue fit vertically");
   check(640*scale>=2424,"world width can fill landscape");
   check(scale==4,"Pixel 9 landscape uses 4x pixels");
  }
  check(ControllerLayout.landscapeScale(1080,2424,5,true)==5,"portrait manual zoom unchanged");
  System.out.println("Backbone/FlipPad/touch policy and Pixel 9 landscape geometry passed");
 }
}
""")
 subprocess.run(['javac','-d',tmp,str(p/'ControllerLayout.java'),str(p/'Check.java')],check=True)
 subprocess.run(['java','-cp',tmp,package+'.Check'],check=True)
