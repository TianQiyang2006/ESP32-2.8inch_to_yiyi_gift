#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "src/touch_transform.h"

TFT_eSPI lcd;
Preferences prefs;
bool inverted=true,bgr=true,calibrating=false,downBefore=false,waitingRelease=false;
int step=0,samples=0;
float sumX=0,sumY=0;
int recentX[12]={},recentY[12]={};
uint32_t physicalPresses=0;
TouchTransform::Point targets[5]={{0,0,45,42},{0,0,274,42},{0,0,274,197},{0,0,45,197},{0,0,160,120}};
TouchTransform::Affine transform;
String input;

void panelMode(){lcd.writecommand(0x36);lcd.writedata(bgr?0x28:0x20);lcd.invertDisplay(inverted);}
void pattern(){
 calibrating=false;panelMode();lcd.fillScreen(TFT_WHITE);
 lcd.setTextColor(TFT_BLACK,TFT_WHITE);lcd.drawString("ILI9341 320 x 240 / TFT_eSPI",5,2,2);
 uint16_t colors[]={TFT_RED,TFT_GREEN,TFT_BLUE,TFT_WHITE,TFT_BLACK};
 const char *names[]={"RED","GREEN","BLUE","WHITE","BLACK"};
 for(int i=0;i<5;i++){lcd.fillRect(i*64,24,64,45,colors[i]);lcd.setTextColor(i==4?TFT_WHITE:TFT_BLACK,colors[i]);lcd.drawCentreString(names[i],i*64+32,37,1);}
 alignas(4) uint16_t line[320];lcd.setSwapBytes(true);
 for(int y=75;y<120;y++){for(int x=0;x<320;x++)line[x]=lcd.color565(x*255/319,(x+y)*255/438,255-x*255/319);lcd.pushImage(0,y,320,1,line);}
 // A 1:1 checkerboard distinguishes native resolution from scaling and missing pixels.
 for(int y=125;y<177;y++){for(int x=0;x<320;x++)line[x]=((x+y)&1)?TFT_BLACK:TFT_WHITE;lcd.pushImage(0,y,320,1,line);}
 lcd.setTextColor(TFT_BLACK,TFT_WHITE);lcd.drawString("1 pixel checker / no scaling",5,181,2);
 lcd.drawString("Calibration starts by USB command",5,207,1);
 lcd.drawRect(0,0,320,240,TFT_BLACK);
 Serial.printf("PATTERN invert=%d bgr=%d MADCTL=%02X PIXFMT=%02X\n",inverted,bgr,lcd.readcommand8(0x0b),lcd.readcommand8(0x0c));
 // Read GRAM back across block/row boundaries; this is separate from physical colour QA.
 int good=0,total=0;
 for(int y=125;y<177;y+=7)for(int x=0;x<320;x+=13){uint16_t expected=x==0?TFT_BLACK:((x+y)&1)?TFT_BLACK:TFT_WHITE;uint16_t actual=lcd.readPixel(x,y);good+=actual==expected;total++;if(actual!=expected)Serial.printf("GRAM mismatch %d,%d expected=%04X actual=%04X\n",x,y,expected,actual);}
 Serial.printf("GRAM checker readback %d/%d\n",good,total);
}
void drawTarget(){
 lcd.fillScreen(TFT_WHITE);lcd.setTextColor(TFT_BLACK,TFT_WHITE);
 int titleY=step==4?16:78;
 char title[40];snprintf(title,sizeof(title),"HOLD TARGET %d / 5",step+1);lcd.drawCentreString(title,160,titleY,2);
 lcd.drawCentreString("Hold centre 0.5 sec, then release",160,titleY+27,1);
 lcd.drawCentreString("Use one finger; avoid sliding",160,titleY+47,1);
 int x=targets[step].x,y=targets[step].y;
 lcd.drawCircle(x,y,15,TFT_BLACK);lcd.drawFastHLine(x-18,y,37,TFT_BLACK);lcd.drawFastVLine(x,y-18,37,TFT_BLACK);
 lcd.fillCircle(x,y,2,TFT_RED);
}
void saveTransform(){
 prefs.putFloat("affax",transform.ax);prefs.putFloat("affbx",transform.bx);prefs.putFloat("affcx",transform.cx);
 prefs.putFloat("affay",transform.ay);prefs.putFloat("affby",transform.by);prefs.putFloat("affcy",transform.cy);
 prefs.putUChar("affver",2);prefs.putBool("affok",true);prefs.putUChar("touch",0);prefs.putBool("calok",false);
 prefs.putUChar("panelrev",2);prefs.putUChar("orient",1);prefs.putBool("bgr",bgr);prefs.putBool("invert",inverted);
}
void calibrationResult(){
 TouchTransform::Affine candidate;
 bool ok=TouchTransform::fit(targets,4,candidate);float worst=0;
 if(ok)for(const auto &p:targets)worst=std::max(worst,TouchTransform::error(candidate,p));
 ok=ok&&worst<=10;
 calibrating=false;lcd.fillScreen(TFT_WHITE);lcd.setTextColor(ok?TFT_DARKGREEN:TFT_RED,TFT_WHITE);
 lcd.drawCentreString(ok?"CALIBRATION SAVED":"CALIBRATION RETRY",160,45,2);
 char detail[40];snprintf(detail,sizeof(detail),"Max error %.1f pixels",worst);lcd.drawCentreString(detail,160,78,2);
 if(ok){transform=candidate;saveTransform();lcd.drawCentreString("Touch anywhere: dot follows finger",160,110,1);}
 else lcd.drawCentreString("Tap only the requested cross",160,110,1);
 Serial.printf("CALIBRATION %s maxError=%.2f\n",ok?"PASS":"FAIL",worst);
}
bool readRaw(int &x,int &y){
 Wire.beginTransmission(0x38);Wire.write(0x02);
 if(Wire.endTransmission(false)!=0||Wire.requestFrom((uint8_t)0x38,(uint8_t)5)!=5){while(Wire.available())Wire.read();return false;}
 uint8_t count=Wire.read()&15,xh=Wire.read(),xl=Wire.read(),yh=Wire.read(),yl=Wire.read();
 if(count!=1||(xh>>6)==1)return false;
 x=((xh&15)<<8)|xl;y=((yh&15)<<8)|yl;return true;
}
void status(){
 JsonDocument j;j["version"]="0.2.2-tft-probe";j["uptimeMs"]=millis();j["width"]=lcd.width();j["height"]=lcd.height();j["bgr"]=bgr;j["inverted"]=inverted;j["calibrating"]=calibrating;j["step"]=step;j["calibrated"]=transform.valid;j["physicalTouchPresses"]=physicalPresses;
 auto a=j["affine"].to<JsonArray>();for(float v:{transform.ax,transform.bx,transform.cx,transform.ay,transform.by,transform.cy})a.add(v);
 auto ps=j["targets"].to<JsonArray>();for(const auto &p:targets){auto v=ps.add<JsonArray>();v.add(p.rawX);v.add(p.rawY);v.add(p.x);v.add(p.y);}
 serializeJson(j,Serial);Serial.println();
}
void command(const String &cmd){
 if(cmd=="status")status();
 else if(cmd=="pattern")pattern();
 else if(cmd=="invert"){inverted=!inverted;pattern();}
 else if(cmd=="bgr"){bgr=!bgr;pattern();}
 else if(cmd=="calibrate"){calibrating=true;step=0;samples=0;sumX=sumY=0;waitingRelease=true;drawTarget();Serial.println("CALIBRATION START: top-left, top-right, bottom-right, bottom-left, centre");}
 else if(cmd.length())Serial.println("UNKNOWN "+cmd);
}
void setup(){
 Serial.begin(115200);pinMode(1,OUTPUT);digitalWrite(1,HIGH);
 prefs.begin("yiyi-desk",false);
 lcd.init();lcd.setRotation(1);panelMode();
 ledcSetup(0,20000,8);ledcAttachPin(45,0);ledcWrite(0,prefs.getUChar("brightness",180));
 Wire.begin(16,15,400000);Wire.setTimeOut(10);pinMode(18,OUTPUT);digitalWrite(18,HIGH);delay(20);digitalWrite(18,LOW);delay(20);digitalWrite(18,HIGH);delay(500);pinMode(17,INPUT_PULLUP);
 Wire.beginTransmission(0x38);Serial.printf("CTP address 0x38: %s\n",Wire.endTransmission()==0?"detected":"missing");
 pattern();Serial.println("PROBE READY");
}
void loop(){
 while(Serial.available()){char c=Serial.read();if(c=='\n'){input.trim();command(input);input="";}else if(c!='\r'&&input.length()<100)input+=c;}
 int x=0,y=0;bool down=readRaw(x,y);
 if(down&&!downBefore){++physicalPresses;Serial.printf("RAW %d %d\n",x,y);}
 if(calibrating){
  if(waitingRelease){if(!down)waitingRelease=false;}
  else if(down){recentX[samples%12]=x;recentY[samples%12]=y;++samples;}
  else if(downBefore&&samples>=8){
   int n=std::min(samples,12),minX=4096,maxX=0,minY=4096,maxY=0;sumX=sumY=0;
   for(int i=0;i<n;i++){sumX+=recentX[i];sumY+=recentY[i];minX=std::min(minX,recentX[i]);maxX=std::max(maxX,recentX[i]);minY=std::min(minY,recentY[i]);maxY=std::max(maxY,recentY[i]);}
   samples=0;
   if(maxX-minX<=8&&maxY-minY<=8){targets[step].rawX=sumX/n;targets[step].rawY=sumY/n;Serial.printf("TARGET %d RAW %.2f %.2f\n",step+1,targets[step].rawX,targets[step].rawY);if(++step>=5)calibrationResult();else{drawTarget();waitingRelease=true;}}
   else{Serial.println("TARGET unstable: retry same cross");drawTarget();}
   sumX=sumY=0;
  }
  else if(!down){samples=0;sumX=sumY=0;}
 }else if(transform.valid&&down){int16_t mx,my;if(TouchTransform::map(transform,x,y,mx,my))lcd.fillCircle(mx,my,3,TFT_BLUE);}
 downBefore=down;delay(15);
}
