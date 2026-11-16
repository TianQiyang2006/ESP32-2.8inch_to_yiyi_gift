#include "music_page.h"
#include "../music_player.h"
#include "../app_state.h"
#include "../gift_assets/gift_fonts.h"
#include "../ui_widgets.h"
#include "page_api.h"
#include "assets/music_art.h"

namespace MusicPage {
namespace {
constexpr uint32_t Cream=0xFAF8F0,Ink=0x244B3C,Green=0x315E48,Mid=0x62745F,Sage=0xE8EDDE,Gold=0xE5AF61,Dark=0x203E32,White=0xFFFCF1;
enum View {Player,List,Lyrics};View view=Player,builtView=Player;bool volumeOpen=false,modeOpen=false;
enum Action {Back=1,Volume,Mode,Previous,Pause,Next,Queue,LyricView,CloseVolume,Minus,Plus,Preset20,Preset40,Preset60,Tab0,Tab1,ReturnPlayer,Retry,CloseMode,Mode0,Mode1,Mode2};
lv_obj_t *root=nullptr,*titleLabel=nullptr,*artistLabel=nullptr,*lyricLabels[4]{},*pauseLabel=nullptr,*volumeLabel=nullptr,*slider=nullptr,*elapsed=nullptr,*total=nullptr,*listBadge=nullptr,*modeLabel=nullptr,*volumeNumber=nullptr;
lv_obj_t *scroll=nullptr,*rows[5]{},*rowName[5]{},*rowArtist[5]{},*rowIndex[5]{},*rowDuration[5]{};
unsigned builtCount=0;int lastTrack=-2;bool refreshing=false;
lv_obj_t *volumeSlider=nullptr,*presets[3]{},*presetLabels[3]{};
lv_obj_t *modeCaption=nullptr,*modeBadge=nullptr,*lyricViewport=nullptr;
int pressedTrack=-1,pressedPlaylist=-1;lv_obj_t *pressedRow=nullptr;unsigned listScroll[8]{};
String lastLyric;
const char *modeName(unsigned value){const char *names[]={"列表循环","单曲循环","随机播放"};return names[value<3?value:0];}
void rebuild(){renderPending=true;lastInputAt=millis();}
lv_obj_t *box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color,int radius=0){
 auto*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;
}
lv_obj_t *label(lv_obj_t*p,const String&value,int x,int y,int w,const lv_font_t*f=&fd_sans_bold_12,uint32_t color=Ink){
 auto*o=lv_label_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,f->line_height);lv_obj_set_style_text_font(o,f,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_label_set_text(o,value.c_str());lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
void set(lv_obj_t*o,const String&value){if(o&&String(lv_label_get_text(o))!=value)lv_label_set_text(o,value.c_str());}
void center(lv_obj_t*o){lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);}
void image(lv_obj_t*p,const lv_img_dsc_t*art,int x,int y){auto*o=lv_img_create(p);lv_img_set_src(o,art);lv_obj_set_pos(o,x,y);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);}
String clockText(unsigned ms){char b[16];snprintf(b,sizeof(b),"%02u:%02u",ms/60000,(ms/1000)%60);return b;}
void populateRows();
void event(lv_event_t*e){
 int action=(intptr_t)lv_event_get_user_data(e);auto code=lv_event_get_code(e);
 if(action>=1000&&code==LV_EVENT_PRESS_LOST){pressedTrack=pressedPlaylist=-1;pressedRow=nullptr;return;}
 if(action>=1000&&code==LV_EVENT_PRESSED){
  auto state=MusicPlayer::state();unsigned row=(uintptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));const auto &list=MusicPlayer::playlist(state.playlist);
  pressedTrack=row<list.count?list.tracks[row]:-1;pressedPlaylist=state.playlist;pressedRow=lv_event_get_current_target(e);return;
 }
 if(code!=LV_EVENT_CLICKED)return;lastInputAt=millis();auto state=MusicPlayer::state();
 if(action>=1000){int target=pressedRow==lv_event_get_current_target(e)&&pressedPlaylist==state.playlist?pressedTrack:-1;pressedTrack=pressedPlaylist=-1;pressedRow=nullptr;if(target>=0){MusicPlayer::play(target);view=Player;rebuild();}return;}
 switch(action){
 case Back:if(!back()){lv_event_t forwarded=*e;forwarded.user_data=(void*)(uintptr_t)Ui::Back;Ui::dispatchAction(&forwarded);}break;
 case Volume:volumeOpen=true;rebuild();break;
 case Mode:modeOpen=true;rebuild();break;
 case CloseMode:modeOpen=false;rebuild();break;
 case Mode0:case Mode1:case Mode2:MusicPlayer::setMode(action-Mode0);modeOpen=false;rebuild();break;
 case Previous:MusicPlayer::next(-1);refresh(true);break;
 case Pause:MusicPlayer::toggle();refresh(true);break;
 case Next:MusicPlayer::next();refresh(true);break;
 case Queue:view=List;rebuild();break;
 case LyricView:view=Lyrics;rebuild();break;
 case CloseVolume:volumeOpen=false;rebuild();break;
 case Minus:MusicPlayer::setVolume(state.volume>5?state.volume-5:0);refresh(true);break;
 case Plus:MusicPlayer::setVolume(state.volume+5);refresh(true);break;
 case Preset20:case Preset40:case Preset60:MusicPlayer::setVolume((action-Preset20+1)*20);refresh(true);break;
 case Tab0:case Tab1:MusicPlayer::selectPlaylist(action-Tab0);rebuild();break;
 case ReturnPlayer:view=Player;rebuild();break;
 // 歌词行：正常时打开歌词页；错误态下同一位置就是"点击重试"（P1-1）。
 case Retry:{auto st=MusicPlayer::state();if(!st.ready){MusicPlayer::open();refresh(true);}else if(st.error.isEmpty()){view=Lyrics;rebuild();}else if(!st.playing){MusicPlayer::toggle();refresh(true);}break;}
 }
}
lv_obj_t *hit(lv_obj_t*p,int x,int y,int w,int h,int action,uint32_t color=0,int radius=0){
 auto*o=box(p,x,y,w,h,color,radius);if(!color)lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
 lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(o,event,LV_EVENT_ALL,(void*)(intptr_t)action);return o;
}
lv_obj_t *symbol(lv_obj_t*p,const char*s,int x,int y,int w,int h,int action,uint32_t color=Green,bool filled=false){
 auto*b=hit(p,x,y,w,h,action,filled?color:0,LV_RADIUS_CIRCLE);
 auto*l=label(b,s,0,0,w,&lv_font_montserrat_20,filled?White:color);center(l);lv_obj_align(l,LV_ALIGN_CENTER,0,0);return l;
}
void seekEvent(lv_event_t*e){if(refreshing||lv_event_get_code(e)!=LV_EVENT_RELEASED)return;lastInputAt=millis();auto s=MusicPlayer::state();MusicPlayer::seek((uint64_t)s.duration*lv_slider_get_value(lv_event_get_target(e))/1000);}
void volumeEvent(lv_event_t*e){if(refreshing)return;MusicPlayer::setVolume(lv_slider_get_value(lv_event_get_target(e)));lastInputAt=millis();refresh(true);}
void header(bool dark=false){
 auto c=dark?White:Ink;symbol(root,LV_SYMBOL_LEFT,0,0,48,40,Back,c);
 auto s=MusicPlayer::state();String title=dark&&s.track>=0?MusicPlayer::track(s.track).title:(view==List?"播放列表":"音乐");
 titleLabel=label(root,title,52,9,dark?164:view==List?180:44,&fd_sans_bold_14_ui,c);
 if(dark){artistLabel=label(root,s.track>=0?MusicPlayer::track(s.track).artist:"",225,11,79,&fd_sans_bold_10,0xBCCCB1);lv_obj_set_style_text_align(artistLabel,LV_TEXT_ALIGN_RIGHT,0);}
 else if(view==List){modeCaption=label(root,modeName(s.mode),237,11,68,&fd_sans_bold_10,Mid);lv_obj_set_style_text_align(modeCaption,LV_TEXT_ALIGN_RIGHT,0);}
 else{
  auto*modeButton=hit(root,96,0,148,36,Mode);
  modeCaption=label(modeButton,modeName(s.mode),4,11,140,&fd_sans_bold_10,Mid);
  auto*b=hit(root,250,0,70,36,Volume);
  auto*icon=label(b,LV_SYMBOL_VOLUME_MAX,8,0,26,&lv_font_montserrat_14,Green);
  lv_obj_set_height(icon,36);lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_pad_top(icon,11,0);
  volumeLabel=label(b,"40",40,11,24,&fd_sans_bold_10,Green);
  lv_obj_set_style_text_align(volumeLabel,LV_TEXT_ALIGN_CENTER,0);
 }
}
void progress(bool dark=false){
 slider=lv_slider_create(root);lv_obj_remove_style_all(slider);lv_obj_set_pos(slider,18,170);lv_obj_set_size(slider,284,3);lv_slider_set_range(slider,0,1000);lv_obj_set_ext_click_area(slider,8);
 lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_bg_color(slider,lv_color_hex(dark?0x3D5B48:0xDDE4D2),LV_PART_MAIN);lv_obj_set_style_radius(slider,2,LV_PART_MAIN);
 lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_bg_color(slider,lv_color_hex(dark?Gold:Green),LV_PART_INDICATOR);lv_obj_set_style_radius(slider,2,LV_PART_INDICATOR);
 lv_obj_set_style_bg_opa(slider,LV_OPA_COVER,LV_PART_KNOB);lv_obj_set_style_bg_color(slider,lv_color_hex(Gold),LV_PART_KNOB);lv_obj_set_style_radius(slider,LV_RADIUS_CIRCLE,LV_PART_KNOB);lv_obj_set_style_pad_all(slider,2,LV_PART_KNOB);lv_obj_add_event_cb(slider,seekEvent,LV_EVENT_RELEASED,nullptr);
 elapsed=label(root,"00:00",18,178,60,&fd_mont_9,dark?0xA7B79E:Mid);total=label(root,"00:00",250,178,52,&fd_mont_9,dark?0xA7B79E:Mid);lv_obj_set_style_text_align(total,LV_TEXT_ALIGN_RIGHT,0);
}
void controls(bool dark=false){
 auto c=dark?0xD0DDC8:Green;
 modeLabel=symbol(root,LV_SYMBOL_LOOP,16,190,40,48,Mode,c);
 modeBadge=label(root,"1",42,195,11,&fd_mont_bold_9,dark?Gold:Green);center(modeBadge);
 symbol(root,LV_SYMBOL_PREV,76,190,40,48,Previous,c);
 pauseLabel=symbol(root,LV_SYMBOL_PAUSE,136,188,48,48,Pause,dark?Gold:Green,true);
 if(dark)lv_obj_set_style_text_color(pauseLabel,lv_color_hex(Dark),0);
 symbol(root,LV_SYMBOL_NEXT,204,190,40,48,Next,c);symbol(root,LV_SYMBOL_LIST,264,190,40,48,Queue,c);
}
void volumePanel(){
 auto*overlay=hit(root,0,0,320,240,CloseVolume,0x15271C);lv_obj_set_style_bg_opa(overlay,105,0);
 auto*sheet=box(overlay,0,105,320,151,Cream,18);lv_obj_add_flag(sheet,LV_OBJ_FLAG_CLICKABLE);
 box(sheet,141,7,38,3,0xD6DDCB,2);label(sheet,"音量",20,24,70,&fd_sans_bold_16,Ink);
 volumeNumber=label(sheet,"40",213,20,57,&fd_sans_bold_22,Green);lv_obj_set_style_text_align(volumeNumber,LV_TEXT_ALIGN_RIGHT,0);
 symbol(sheet,LV_SYMBOL_CLOSE,276,13,36,32,CloseVolume,Mid);
 symbol(sheet,LV_SYMBOL_MINUS,14,49,40,40,Minus,Green);
 auto*vs=volumeSlider=lv_slider_create(sheet);lv_obj_set_pos(vs,65,66);lv_obj_set_size(vs,190,5);lv_slider_set_range(vs,0,100);lv_slider_set_value(vs,MusicPlayer::state().volume,LV_ANIM_OFF);lv_obj_set_ext_click_area(vs,12);
 lv_obj_set_style_bg_color(vs,lv_color_hex(0xDDE4D2),LV_PART_MAIN);lv_obj_set_style_bg_color(vs,lv_color_hex(Green),LV_PART_INDICATOR);lv_obj_set_style_bg_color(vs,lv_color_hex(Green),LV_PART_KNOB);lv_obj_add_event_cb(vs,volumeEvent,LV_EVENT_VALUE_CHANGED,nullptr);
 symbol(sheet,LV_SYMBOL_PLUS,266,49,40,40,Plus,Green);
 const char*names[]={"20","40","60"};for(int i=0;i<3;++i){auto*b=presets[i]=hit(sheet,18+i*98,96,88,34,Preset20+i,Sage,9);auto*l=presetLabels[i]=label(b,names[i],0,9,88,&fd_sans_bold_12,Green);center(l);}
}
void modePanel(){
 auto*overlay=hit(root,0,0,320,240,CloseMode,0x15271C);lv_obj_set_style_bg_opa(overlay,105,0);
 auto*sheet=box(overlay,28,26,264,188,Cream,16);lv_obj_add_flag(sheet,LV_OBJ_FLAG_CLICKABLE);
 label(sheet,"播放模式",16,13,160,&fd_sans_bold_16);symbol(sheet,LV_SYMBOL_CLOSE,220,4,36,36,CloseMode,Mid);
 auto state=MusicPlayer::state();
 for(unsigned i=0;i<3;++i){bool active=state.mode==i;auto*b=hit(sheet,12,45+i*43,240,37,Mode0+i,active?Green:Sage,9);label(b,modeName(i),14,9,190,&fd_sans_bold_12,active?White:Green);if(active)label(b,LV_SYMBOL_OK,211,10,18,&lv_font_montserrat_14,White);}
}
void populateRows(){
 if(!scroll)return;auto s=MusicPlayer::state();auto &list=MusicPlayer::playlist(s.playlist);
 // Elastic overscroll can be negative; keep the virtual rows inside the catalog.
 int offset=std::max(0,std::min(int(lv_obj_get_scroll_y(scroll)),std::max(0,int(list.count)*40-120)));unsigned first=offset/40;
 for(unsigned i=0;i<5;++i){unsigned n=first+i;if(n>=list.count){lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN);continue;}
  // A pressed widget keeps its identity until RELEASED/CLICKED or PRESS_LOST.
  if(rows[i]==pressedRow&&lv_obj_has_state(rows[i],LV_STATE_PRESSED))continue;
  lv_obj_clear_flag(rows[i],LV_OBJ_FLAG_HIDDEN);lv_obj_set_y(rows[i],n*40);lv_obj_set_user_data(rows[i],(void*)(uintptr_t)n);
  const auto&t=MusicPlayer::track(list.tracks[n]);bool active=(int)list.tracks[n]==s.track;
  lv_obj_set_style_bg_opa(rows[i],active?LV_OPA_COVER:LV_OPA_TRANSP,0);set(rowName[i],t.title);set(rowArtist[i],t.artist);set(rowIndex[i],active?String(LV_SYMBOL_AUDIO):String(n+1));set(rowDuration[i],active?String(s.remembered?"待续播":s.paused?"已暂停":s.loading||s.buffering?"缓冲中":"播放中"):String());
 }
}
void scrollEvent(lv_event_t*){auto s=MusicPlayer::state();if(scroll&&s.playlist>=0&&s.playlist<8)listScroll[s.playlist]=std::max(0,int(lv_obj_get_scroll_y(scroll)));populateRows();}
void listPage(){
 header();auto s=MusicPlayer::state();
 for(unsigned i=0;i<MusicPlayer::playlistCount()&&i<2;++i){bool active=(int)i==s.playlist;int x=i?198:12,w=i?110:178;
  auto*b=hit(root,x,40,w,32,Tab0+i,active?Green:Sage,10);label(b,MusicPlayer::playlist(i).name,10,7,w-38,&fd_sans_bold_12,active?White:Green);
  auto*l=label(b,String(MusicPlayer::playlist(i).count),w-33,9,25,&fd_mont_9,active?White:Green);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_RIGHT,0);
 }
 scroll=box(root,8,78,308,120,Cream);lv_obj_add_flag(scroll,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(scroll,LV_DIR_VER);lv_obj_set_scrollbar_mode(scroll,LV_SCROLLBAR_MODE_AUTO);lv_obj_set_style_bg_color(scroll,lv_color_hex(0xACBB9F),LV_PART_SCROLLBAR);
 auto*content=box(scroll,0,0,300,MusicPlayer::playlist(s.playlist).count*40,Cream);lv_obj_clear_flag(content,LV_OBJ_FLAG_CLICKABLE);
 for(int i=0;i<5;++i){rows[i]=hit(content,4,i*40,296,38,1000,Sage,8);rowName[i]=label(rows[i],"",36,2,196,&fd_sans_bold_14_ui);rowArtist[i]=label(rows[i],"",37,22,196,&fd_sans_bold_10,Mid);rowIndex[i]=label(rows[i],"",10,13,24,&lv_font_montserrat_12,Green);rowDuration[i]=label(rows[i],"",239,13,51,&fd_sans_bold_10,Mid);lv_obj_set_style_text_align(rowDuration[i],LV_TEXT_ALIGN_RIGHT,0);}
 lv_obj_add_event_cb(scroll,scrollEvent,LV_EVENT_SCROLL,nullptr);populateRows();
 lv_obj_update_layout(scroll);if(s.playlist>=0&&s.playlist<8)lv_obj_scroll_to_y(scroll,listScroll[s.playlist],LV_ANIM_OFF);populateRows();
 if(!MusicPlayer::playlist(s.playlist).count){auto*b=hit(root,20,92,280,84,Retry);lyricLabels[0]=label(b,"",0,18,280,&fd_sans_bold_12,Mid);center(lyricLabels[0]);lv_label_set_long_mode(lyricLabels[0],LV_LABEL_LONG_WRAP);lv_obj_set_height(lyricLabels[0],LV_SIZE_CONTENT);}
 auto*mini=hit(root,0,200,218,40,ReturnPlayer,0xEEF0E4);image(mini,&music_cover_small,14,7);titleLabel=label(mini,"",51,4,150,&fd_sans_bold_12);artistLabel=label(mini,"",52,22,150,&fd_sans_bold_10,Mid);
 box(root,218,200,102,40,0xEEF0E4);pauseLabel=symbol(root,LV_SYMBOL_PAUSE,224,200,40,40,Pause,Green,true);symbol(root,LV_SYMBOL_NEXT,268,200,40,40,Next,Green);
}
}
void enter(){view=Player;volumeOpen=modeOpen=false;MusicPlayer::open();}
void build(lv_obj_t *parent){
 builtView=view;
 root=parent;titleLabel=artistLabel=pauseLabel=volumeLabel=slider=elapsed=total=listBadge=modeLabel=volumeNumber=scroll=nullptr;for(auto&l:lyricLabels)l=nullptr;lastTrack=-2;builtCount=MusicPlayer::trackCount();
 volumeSlider=nullptr;for(auto&p:presets)p=nullptr;for(auto&p:presetLabels)p=nullptr;
 modeCaption=modeBadge=lyricViewport=nullptr;lastLyric="";pressedTrack=pressedPlaylist=-1;pressedRow=nullptr;
 lv_obj_set_style_bg_color(root,lv_color_hex(view==Lyrics?Dark:Cream),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 if(view==List)listPage();
 else if(view==Lyrics){
  header(true);
  lyricLabels[0]=label(root,"",22,44,276,&fd_sans_bold_12,0x91A68C);center(lyricLabels[0]);
  lyricViewport=box(root,20,69,280,68,Dark);lv_obj_add_flag(lyricViewport,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(lyricViewport,LV_DIR_VER);lv_obj_set_scrollbar_mode(lyricViewport,LV_SCROLLBAR_MODE_AUTO);
  lyricLabels[2]=label(lyricViewport,"",2,0,276,&fd_sans_bold_16,White);center(lyricLabels[2]);lv_label_set_long_mode(lyricLabels[2],LV_LABEL_LONG_WRAP);lv_obj_set_height(lyricLabels[2],LV_SIZE_CONTENT);lv_obj_set_style_text_line_space(lyricLabels[2],4,0);
  lyricLabels[3]=label(root,"",22,145,276,&fd_sans_bold_12,0x91A68C);center(lyricLabels[3]);
  box(root,12,91,3,24,Gold,2);progress(true);controls(true);
 }
 else{
  header();auto*record=box(root,55,50,78,78,0x294A3B,LV_RADIUS_CIRCLE);for(int r:{34,30,24,20}){auto*o=box(record,39-r,39-r,r*2,r*2,0x294A3B,LV_RADIUS_CIRCLE);lv_obj_set_style_border_color(o,lv_color_hex(0x49654C),0);lv_obj_set_style_border_width(o,1,0);}image(root,&music_cover,16,45);
  titleLabel=label(root,"音乐",146,48,158,&fd_sans_bold_22);lv_label_set_long_mode(titleLabel,LV_LABEL_LONG_SCROLL_CIRCULAR);artistLabel=label(root,"选择喜欢的歌曲",147,81,155,&fd_sans_bold_12,Mid);
  auto*b=hit(root,144,107,160,21,Queue,Sage,7);label(b,LV_SYMBOL_AUDIO,9,5,17,&lv_font_montserrat_12,Green);listBadge=label(b,"",26,4,125,&fd_sans_bold_10,Green);
  auto*lyricsHit=hit(root,12,136,296,27,Retry);lyricLabels[0]=label(lyricsHit,"",8,5,276,&fd_sans_bold_12,Green);center(lyricLabels[0]);lv_label_set_long_mode(lyricLabels[0],LV_LABEL_LONG_SCROLL_CIRCULAR);
  progress();controls();
 }
 if(volumeOpen)volumePanel();if(modeOpen)modePanel();refresh(true);
}
void refresh(bool){
 if(!root||view!=builtView)return;auto s=MusicPlayer::state();
 if(MusicPlayer::trackCount()!=builtCount){rebuild();return;}
 int display=s.track;
 if(display<0&&s.ready&&MusicPlayer::playlist(s.playlist).count)display=MusicPlayer::playlist(s.playlist).tracks[0];
 if(display!=lastTrack){lastTrack=display;String name=display>=0?MusicPlayer::track(display).title:"音乐",artist=display>=0?MusicPlayer::track(display).artist:"联网后加载曲库";set(titleLabel,name);set(artistLabel,artist);}
 set(volumeLabel,String(s.volume));set(volumeNumber,String(s.volume));
 refreshing=true;if(volumeSlider&&!lv_obj_has_state(volumeSlider,LV_STATE_PRESSED))lv_slider_set_value(volumeSlider,s.volume,LV_ANIM_OFF);refreshing=false;
 for(unsigned i=0;i<3;++i)if(presets[i]){bool active=s.volume==(i+1)*20;lv_obj_set_style_bg_color(presets[i],lv_color_hex(active?Green:Sage),0);lv_obj_set_style_text_color(presetLabels[i],lv_color_hex(active?White:Green),0);}
 if(pauseLabel)set(pauseLabel,s.playing&&!s.paused?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
 if(modeLabel)set(modeLabel,s.mode==2?LV_SYMBOL_SHUFFLE:LV_SYMBOL_LOOP);
 if(modeCaption)set(modeCaption,modeName(s.mode));
 if(modeBadge){if(s.mode==1)lv_obj_clear_flag(modeBadge,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(modeBadge,LV_OBJ_FLAG_HIDDEN);}
 if(listBadge)set(listBadge,MusicPlayer::playlist(s.playlist).name);
 if(elapsed)set(elapsed,clockText(s.position));if(total)set(total,clockText(s.duration));
 refreshing=true;if(slider&&!lv_obj_has_state(slider,LV_STATE_PRESSED))lv_slider_set_value(slider,s.duration?(uint64_t)s.position*1000/s.duration:0,LV_ANIM_OFF);refreshing=false;
 if(view==Lyrics){
  set(lyricLabels[0],MusicPlayer::lyric(-1));set(lyricLabels[3],MusicPlayer::lyric(1));
  String words=MusicPlayer::lyric();
  if(words.isEmpty())words=s.loading?"歌词加载中…":s.remembered?"点击播放，继续上次的音乐":!s.lyrics?"暂无歌词":s.lyricIndex<0?"前奏，等待歌词":"伴奏";
  if(words!=lastLyric){lastLyric=words;set(lyricLabels[2],words);lv_obj_update_layout(lyricLabels[2]);lv_obj_set_y(lyricLabels[2],std::max(0,(68-int(lv_obj_get_height(lyricLabels[2])))/2));lv_obj_scroll_to_y(lyricViewport,0,LV_ANIM_OFF);}
 }
 else if(view==Player){String words=MusicPlayer::lyric();if(!s.error.isEmpty())words=s.error;else if(s.loading)words="正在准备歌曲…";else if(s.buffering&&!s.paused)words="正在缓冲，马上播放…";else if(!s.ready)words="正在连接曲库…";else if(s.saveFailed)words="播放记忆保存失败，稍后重试";else if(s.remembered)words="已记住上次进度，点击继续";else if(words.isEmpty())words=s.track<0?"点击播放，听喜欢的音乐":s.paused?"已暂停，点击继续":s.lyrics?(s.lyricIndex<0?"前奏，等待歌词":"伴奏"):"暂无歌词";set(lyricLabels[0],words);}
 if(view==List){populateRows();if(!MusicPlayer::playlist(s.playlist).count)set(lyricLabels[0],s.error.isEmpty()?String("正在加载曲库，点击重试"):s.error);}
}
bool back(){if(modeOpen){modeOpen=false;rebuild();return true;}if(volumeOpen){volumeOpen=false;rebuild();return true;}if(view!=Player){view=Player;rebuild();return true;}return false;}
bool canBack(){return modeOpen||volumeOpen||view!=Player;}
bool inputActive(){return root&&view==builtView&&((slider&&lv_obj_has_state(slider,LV_STATE_PRESSED))||(volumeSlider&&lv_obj_has_state(volumeSlider,LV_STATE_PRESSED))||(lyricViewport&&lv_obj_is_scrolling(lyricViewport)));}
void cancel(){root=nullptr;pressedTrack=pressedPlaylist=-1;pressedRow=nullptr;}
}
