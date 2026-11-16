#include "radio_page.h"
#include "../radio_player.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include <algorithm>

namespace RadioPage {
namespace {
constexpr uint32_t Paper=0xFAF8F0,Forest=0x294E3D,Deep=0x203E32,Ink=0x294B3B;
constexpr uint32_t Sage=0xE9EDDF,Muted=0x71806C,Gold=0xE7B66D,White=0xFFFBEF,Line=0xDCE2D0;
enum View {Player,Stations};
enum Action {Back=1,SelectStations,ShowFavorites,AllTab,FavoriteTab,Previous,Toggle,Next,
             Favorite,Volume,CloseVolume,Minus,Plus,Mute,Quiet,Normal,ReturnPlayer,Retry,StationRow=100};
View view=Player,builtView=Player;
bool favoritesOnly=false,volumeOpen=false,updating=false;
lv_obj_t *canvas=nullptr,*stationName=nullptr,*programTitle=nullptr,*statusText=nullptr,*hintText=nullptr;
lv_obj_t *playText=nullptr,*volumeText=nullptr,*heartText=nullptr,*heartButton=nullptr,*onAirDot=nullptr,*liveText=nullptr;
lv_obj_t *stationNumber=nullptr,*volumeSlider=nullptr,*volumeNumber=nullptr,*stationScroll=nullptr,*categoryText=nullptr,*rowPlaying=nullptr;
lv_obj_t *presetButtons[3]{},*presetLabels[3]{};
unsigned builtFavorites=0,builtStations=0;
uint32_t builtCatalogRevision=0;
int builtStation=-2;

lv_obj_t *box(lv_obj_t *parent,int x,int y,int w,int h,uint32_t color,int radius=0){
 auto *obj=lv_obj_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
 lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
 lv_obj_set_style_radius(obj,radius,0);lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
lv_obj_t *text(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=&fd_sans_bold_14_ui,uint32_t color=Ink){
 auto *obj=lv_label_create(parent);lv_obj_remove_style_all(obj);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,font->line_height);
 lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
 lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);lv_label_set_text(obj,value.c_str());lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);return obj;
}
void set(lv_obj_t *obj,const String &value){if(obj&&String(lv_label_get_text(obj))!=value)lv_label_set_text(obj,value.c_str());}
void middle(lv_obj_t *obj){lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);}
void rebuild(){renderPending=true;lastInputAt=millis();}
unsigned favoriteCount(){unsigned n=0;for(unsigned i=0;i<RadioPlayer::stationCount();++i)if(RadioPlayer::isFavorite(i))++n;return n;}
void onAction(lv_event_t *event);
lv_obj_t *hit(lv_obj_t *parent,int x,int y,int w,int h,int action,uint32_t color=Paper,int radius=0){
 auto *obj=box(parent,x,y,w,h,color,radius);lv_obj_add_flag(obj,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(obj,onAction,LV_EVENT_CLICKED,(void*)(intptr_t)action);
 return obj;
}
lv_obj_t *symbol(lv_obj_t *parent,const char *glyph,int x,int y,int w,int h,int action,uint32_t foreground=Forest,uint32_t background=Paper,int size=20){
 auto *obj=hit(parent,x,y,w,h,action,background,LV_RADIUS_CIRCLE);
 auto *caption=text(obj,glyph,0,0,w,size==14?&lv_font_montserrat_14:&lv_font_montserrat_20,foreground);
 middle(caption);lv_obj_align(caption,LV_ALIGN_CENTER,0,0);return caption;
}
lv_obj_t *pill(lv_obj_t *parent,const String &caption,int x,int y,int w,int action,bool active=false){
 auto *obj=hit(parent,x,y,w,32,action,active?Forest:Sage,10);
 auto *title=text(obj,caption,0,0,w,&fd_sans_bold_12_ui,active?White:Forest);middle(title);lv_obj_align(title,LV_ALIGN_CENTER,0,0);return obj;
}
bool active(const RadioPlayer::State &state){return state.playing||state.connecting||state.buffering;}
String status(const RadioPlayer::State &state){
 if(!state.wifi)return "还没联网";
 if(state.connecting)return state.retries?"正在重新连接":"正在连接电台";
 if(state.buffering)return "正在缓冲";
 if(!state.error.isEmpty())return "暂时连接不上";
 return state.playing?"正在直播":"等待收听";
}
String stationTitle(const RadioPlayer::State &state){return state.station>=0&&unsigned(state.station)<RadioPlayer::stationCount()?RadioPlayer::station(state.station).name:"选择一个电台";}
String category(const RadioPlayer::State &state){
 if(state.station<0||unsigned(state.station)>=RadioPlayer::stationCount())return "随心收听";
 String value=RadioPlayer::station(state.station).tag;int divider=value.indexOf(" · ");
 if(divider>=0)value=value.substring(0,divider);return value;
}
void header(const char *title,bool selector){
 symbol(canvas,LV_SYMBOL_LEFT,0,0,40,38,Back);
 text(canvas,title,43,10,166,&fd_sans_bold_14_ui);
 if(selector){auto *button=pill(canvas,"选台",242,3,66,SelectStations);(void)button;}
}
void speaker(lv_obj_t *parent,int x,int y,int diameter,bool small=false){
 const uint32_t rim=0x466550,body=0x203E32,slot=0x69816A;
 auto *outer=box(parent,x,y,diameter,diameter,rim,LV_RADIUS_CIRCLE);
 auto *inner=box(outer,3,3,diameter-6,diameter-6,body,LV_RADIUS_CIRCLE);
 lv_obj_set_style_border_width(inner,1,0);lv_obj_set_style_border_color(inner,lv_color_hex(0x375B46),0);
 const int gap=small?5:6,margin=small?8:13;
 for(int offset=margin;offset<diameter-margin;offset+=gap){
  int delta=abs(offset-diameter/2);int inset=delta>diameter/4?6:delta>diameter/8?2:0;
  box(outer,margin+inset,offset,diameter-2*margin-2*inset,small?1:2,slot,1);
 }
}
void player(){
 header("在线 FM",true);
 auto *radio=box(canvas,12,42,296,120,Forest,15);
 speaker(radio,13,23,75);
 text(radio,"YIYI RADIO",15,103,73,&fd_mont_bold_9,0xB2C2A4);
 onAirDot=box(radio,101,15,5,5,Gold,LV_RADIUS_CIRCLE);
 liveText=text(radio,"LIVE",112,10,38,&fd_mont_bold_9,Gold);
 auto state=RadioPlayer::state();
 categoryText=text(radio,category(state),158,10,77,&fd_sans_bold_12_ui,0xBCCAB2);
 lv_obj_set_style_text_align(categoryText,LV_TEXT_ALIGN_RIGHT,0);
 heartButton=hit(radio,254,3,36,36,Favorite,Forest,LV_RADIUS_CIRCLE);
 heartText=text(heartButton,"♡",0,4,36,&fd_sans_bold_16,Gold);middle(heartText);
 stationName=text(radio,stationTitle(state),100,37,182,&fd_sans_bold_16_ui,White);
 lv_label_set_long_mode(stationName,LV_LABEL_LONG_SCROLL_CIRCULAR);
 programTitle=text(radio,"让喜欢的声音陪着你",101,64,180,&fd_sans_bold_12_ui,0xCFD9C2);
 lv_label_set_long_mode(programTitle,LV_LABEL_LONG_SCROLL_CIRCULAR);
 box(radio,101,88,179,1,0x527058);
 for(int i=0;i<25;++i)box(radio,102+i*7,88,1,i%4==0?8:4,0x8BA182);
 box(radio,145,86,2,12,Gold,1);
 statusText=text(radio,"等待收听",101,103,124,&fd_sans_bold_12_ui,0xCFD9C2);
 stationNumber=text(radio,"01 / 08",232,105,49,&fd_mont_bold_9,0xBBCBB1);lv_obj_set_style_text_align(stationNumber,LV_TEXT_ALIGN_RIGHT,0);
 auto *hint=hit(canvas,12,164,296,32,Retry);
 hintText=text(hint,"点击播放，开始收听",2,8,292,&fd_sans_bold_12_ui,Muted);middle(hintText);
 auto *vol=hit(canvas,8,198,50,40,Volume);
 text(vol,LV_SYMBOL_VOLUME_MAX,2,12,20,&lv_font_montserrat_14,Muted);
 volumeText=text(vol,"35",25,12,25,&fd_mont_bold_9,Muted);
 symbol(canvas,LV_SYMBOL_PREV,76,198,40,40,Previous,Forest);
 playText=symbol(canvas,LV_SYMBOL_PLAY,136,190,48,48,Toggle,White,Forest);
 symbol(canvas,LV_SYMBOL_NEXT,204,198,40,40,Next,Forest);
 auto *favorites=hit(canvas,259,198,53,40,ShowFavorites);
 auto *favLabel=text(favorites,"收藏",0,12,53,&fd_sans_bold_12_ui,Forest);middle(favLabel);
}
void list(){
 header("电台",false);
 auto state=RadioPlayer::state();
 auto *count=text(canvas,String(RadioPlayer::stationCount())+" 个声音",223,12,85,&fd_sans_bold_12_ui,Muted);lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_RIGHT,0);
 pill(canvas,"全部电台",12,43,144,AllTab,!favoritesOnly);
 pill(canvas,"我的收藏",164,43,144,FavoriteTab,favoritesOnly);
 stationScroll=box(canvas,8,83,304,115,Paper);
 lv_obj_add_flag(stationScroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(stationScroll,LV_DIR_VER);
 lv_obj_set_scrollbar_mode(stationScroll,LV_SCROLLBAR_MODE_AUTO);lv_obj_set_style_bg_color(stationScroll,lv_color_hex(0xB9C6AD),LV_PART_SCROLLBAR);
 unsigned row=0;
 for(unsigned i=0;i<RadioPlayer::stationCount();++i){
  if(favoritesOnly&&!RadioPlayer::isFavorite(i))continue;
  bool selected=state.station==int(i);
  auto *item=hit(stationScroll,4,row*40,296,38,StationRow+i,selected?Sage:Paper,9);
  auto *index=text(item,String(i+1),8,11,25,&fd_mont_bold_13,selected?Forest:0x9BA58E);middle(index);
  text(item,RadioPlayer::station(i).name,40,2,215,&fd_sans_bold_14_ui,Ink);
  text(item,RadioPlayer::station(i).tag,41,22,214,&fd_sans_bold_12_ui,Muted);
  if(selected)rowPlaying=text(item,active(state)?LV_SYMBOL_AUDIO:LV_SYMBOL_PLAY,267,12,23,&lv_font_montserrat_14,Forest);
  else if(RadioPlayer::isFavorite(i))text(item,"♡",266,6,26,&fd_sans_bold_16,0xB28D52);
  ++row;
 }
 if(!row){
  auto *icon=text(stationScroll,"♡",0,11,304,&fd_sans_bold_22,0x9FAE92);middle(icon);
  auto *title=text(stationScroll,favoritesOnly?"把喜欢的声音留在这里":"还没有电台",0,53,304,&fd_sans_bold_14_ui,Forest);middle(title);
  auto *detail=text(stationScroll,favoritesOnly?"收听时点一下爱心，就能收藏":"连接手机后，添加喜欢的电台",0,81,304,&fd_sans_bold_12_ui,Muted);middle(detail);
 }
 auto *mini=hit(canvas,0,204,262,36,ReturnPlayer,0xEEF0E4);
 box(mini,12,10,3,17,Gold,1);
 text(mini,stationTitle(state),23,1,227,&fd_sans_bold_14_ui,Forest);
 text(mini,"返回收听",24,21,225,&fd_sans_bold_12_ui,Muted);
 playText=symbol(canvas,LV_SYMBOL_PLAY,268,202,40,38,Toggle,Forest,0xEEF0E4);
}
void sliderChanged(lv_event_t *event){if(updating)return;lastInputAt=millis();RadioPlayer::setVolume(lv_slider_get_value(lv_event_get_target(event)));refresh(true);}
void volumeSheet(){
 auto *overlay=hit(canvas,0,0,320,240,CloseVolume,0x142B20);lv_obj_set_style_bg_opa(overlay,125,0);
 auto *sheet=box(overlay,0,91,320,149,Paper,18);lv_obj_add_flag(sheet,LV_OBJ_FLAG_CLICKABLE);
 box(sheet,141,7,38,3,Line,2);text(sheet,"音量",20,24,80,&fd_sans_bold_16_ui);
 volumeNumber=text(sheet,"35%",211,24,58,&fd_mont_bold_20,Forest);lv_obj_set_style_text_align(volumeNumber,LV_TEXT_ALIGN_RIGHT,0);
 symbol(sheet,LV_SYMBOL_CLOSE,276,13,36,36,CloseVolume,Muted);
 symbol(sheet,LV_SYMBOL_MINUS,13,53,40,40,Minus,Forest);
 volumeSlider=lv_slider_create(sheet);lv_obj_set_pos(volumeSlider,68,70);lv_obj_set_size(volumeSlider,183,5);lv_slider_set_range(volumeSlider,0,100);
 lv_obj_set_ext_click_area(volumeSlider,13);lv_obj_set_style_bg_color(volumeSlider,lv_color_hex(Line),LV_PART_MAIN);
 lv_obj_set_style_bg_color(volumeSlider,lv_color_hex(Forest),LV_PART_INDICATOR);lv_obj_set_style_bg_color(volumeSlider,lv_color_hex(Forest),LV_PART_KNOB);
 lv_obj_set_style_pad_all(volumeSlider,4,LV_PART_KNOB);lv_obj_add_event_cb(volumeSlider,sliderChanged,LV_EVENT_VALUE_CHANGED,nullptr);
 symbol(sheet,LV_SYMBOL_PLUS,267,53,40,40,Plus,Forest);
 const char *captions[]={"静音","轻声 20","适中 40"};
 for(int i=0;i<3;++i){presetButtons[i]=hit(sheet,18+i*98,103,88,32,Mute+i,Sage,9);presetLabels[i]=text(presetButtons[i],captions[i],0,7,88,&fd_sans_bold_12_ui,Forest);middle(presetLabels[i]);}
}
void onAction(lv_event_t *event){
 int action=(intptr_t)lv_event_get_user_data(event);lastInputAt=millis();auto state=RadioPlayer::state();
 if(action>=StationRow){
  // A phone edit can shift row indices while this view still owns a touch.
  // Rebuild the stale list before accepting another selection.
  if(builtCatalogRevision!=RadioPlayer::stationRevision()){rebuild();return;}
  RadioPlayer::play(action-StationRow);view=Player;rebuild();return;
 }
 switch(action){
 case Back:if(!back()){lv_event_t forwarded=*event;forwarded.user_data=(void*)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);}break;
 case SelectStations:view=Stations;favoritesOnly=false;rebuild();break;
 case ShowFavorites:view=Stations;favoritesOnly=true;rebuild();break;
 case AllTab:favoritesOnly=false;rebuild();break;
 case FavoriteTab:favoritesOnly=true;rebuild();break;
 case Previous:RadioPlayer::next(-1);refresh(true);break;
 case Next:RadioPlayer::next();refresh(true);break;
 case Toggle:RadioPlayer::toggle();refresh(true);break;
 case Favorite:RadioPlayer::toggleFavorite();refresh(true);break;
 case Volume:volumeOpen=true;rebuild();break;
 case CloseVolume:volumeOpen=false;rebuild();break;
 case Minus:RadioPlayer::setVolume(state.volume>5?state.volume-5:0);refresh(true);break;
 case Plus:RadioPlayer::setVolume(std::min(100u,state.volume+5));refresh(true);break;
 case Mute:case Quiet:case Normal:RadioPlayer::setVolume((action-Mute)*20);refresh(true);break;
 case ReturnPlayer:view=Player;rebuild();break;
 case Retry:if(!active(state)&&state.wifi){if(state.station>=0)RadioPlayer::play(state.station);else RadioPlayer::toggle();}refresh(true);break;
 }
}
}

void enter(){view=Player;favoritesOnly=false;volumeOpen=false;RadioPlayer::open();}
void build(lv_obj_t *parent){
 canvas=parent;builtView=view;stationName=programTitle=statusText=hintText=playText=volumeText=heartText=heartButton=onAirDot=liveText=stationNumber=volumeSlider=volumeNumber=stationScroll=categoryText=rowPlaying=nullptr;
 for(auto &p:presetButtons)p=nullptr;for(auto &p:presetLabels)p=nullptr;
 auto state=RadioPlayer::state();builtStation=state.station;builtStations=RadioPlayer::stationCount();builtFavorites=favoriteCount();builtCatalogRevision=RadioPlayer::stationRevision();
 lv_obj_set_style_bg_color(canvas,lv_color_hex(Paper),0);lv_obj_set_style_bg_opa(canvas,LV_OPA_COVER,0);
 if(view==Stations)list();else player();if(volumeOpen)volumeSheet();refresh(true);
}
void refresh(bool){
 if(!canvas||view!=builtView)return;auto state=RadioPlayer::state();
 if(builtCatalogRevision!=RadioPlayer::stationRevision()){rebuild();return;}
 if(view==Stations&&(builtStation!=state.station||builtStations!=RadioPlayer::stationCount()||builtFavorites!=favoriteCount())){rebuild();return;}
 set(stationName,stationTitle(state));
 set(categoryText,category(state));
 set(programTitle,state.title.isEmpty()?String("让喜欢的声音陪着你"):state.title);
 set(statusText,status(state));
 set(playText,active(state)?LV_SYMBOL_STOP:LV_SYMBOL_PLAY);
 set(rowPlaying,active(state)?LV_SYMBOL_AUDIO:LV_SYMBOL_PLAY);
 if(onAirDot)lv_obj_set_style_bg_color(onAirDot,lv_color_hex(state.playing&&!state.buffering?Gold:0x84977C),0);
 if(liveText){set(liveText,state.playing&&!state.buffering?"LIVE":"RADIO");lv_obj_set_style_text_color(liveText,lv_color_hex(state.playing&&!state.buffering?Gold:0xB2C2A4),0);}
 if(heartButton)lv_obj_set_style_bg_color(heartButton,lv_color_hex(state.favorite?Gold:Forest),0);
 if(heartText)lv_obj_set_style_text_color(heartText,lv_color_hex(state.favorite?Deep:Gold),0);
 if(stationNumber){char value[24];snprintf(value,sizeof(value),"%02u / %02u",state.station<0?0:state.station+1,RadioPlayer::stationCount());set(stationNumber,value);}
 String hint=!RadioPlayer::stationCount()?"连接手机，添加一个电台":state.saveFailed?"设置保存失败，稍后重试":!state.wifi?"联网后，点播放开始收听":!state.error.isEmpty()?"连接失败，点这里重试":state.connecting?"正在寻找这个声音…":state.buffering?"稍等一下，马上就好":state.playing?"此刻，让声音陪着你":"点击播放，开始收听";
 set(hintText,hint);
 set(volumeText,String(state.volume));set(volumeNumber,String(state.volume)+"%");
 updating=true;if(volumeSlider&&!lv_obj_has_state(volumeSlider,LV_STATE_PRESSED))lv_slider_set_value(volumeSlider,state.volume,LV_ANIM_OFF);updating=false;
 for(unsigned i=0;i<3;++i)if(presetButtons[i]){bool selected=state.volume==i*20;lv_obj_set_style_bg_color(presetButtons[i],lv_color_hex(selected?Forest:Sage),0);lv_obj_set_style_text_color(presetLabels[i],lv_color_hex(selected?White:Forest),0);}
}
bool back(){if(volumeOpen){volumeOpen=false;rebuild();return true;}if(view==Stations){view=Player;rebuild();return true;}return false;}
bool canBack(){return volumeOpen||view==Stations;}
bool inputActive(){return canvas&&view==builtView&&((volumeSlider&&lv_obj_has_state(volumeSlider,LV_STATE_PRESSED))||(stationScroll&&lv_obj_is_scrolling(stationScroll)));}
void cancel(){canvas=nullptr;volumeSlider=stationScroll=nullptr;}
void createIcon(lv_obj_t *parent,int x,int y){
 auto *icon=box(parent,x,y,56,56,Forest,13);speaker(icon,6,17,29,true);
 box(icon,37,18,12,3,Gold,1);for(int i=0;i<3;++i)box(icon,38+i*4,27,1,9,0xA6BB99);
 box(icon,40,40,6,6,Gold,LV_RADIUS_CIRCLE);box(icon,14,10,30,2,0xC2CFAE,1);
}
}
