#include "gift_ui.h"
#include "gift_data.h"
#include "clock_source.h"
#include "gift_assets/gift_assets.h"
#include "gift_assets/spring_dates.h"
#include "audio_board.h"
#include "xiaozhi_voice.h"
#include "power_status.h"
#include "app_state.h"
#include "touch.h"
#include "ui_widgets.h"
#include "ui_styles.h"
#include "text_util.h"
#include "chinese_layout.h"
#include "gift_lunar.h"
#include "pixel_clock.h"
#include "saved_design/saved_design.h"
#include "saved_design/saved_resources.h"
#include "pages/page_api.h"
#include "pages/mini_apps.h"
#include "pages/dashboard_clock_page.h"
#include "pages/dashboard_clock_settings.h"
#include "pages/timer_page.h"
#include "pages/days_page.h"
#include "pages/assets/ui_status_icons.h"
#include "pages/assets/focus_app_icons.h"
#include "pages/assets/tetris_app_icon.h"
#include "pages/assets/music_art.h"
#include "pages/music_page.h"
#include "pages/radio_page.h"
#include "../desk_logic.h"
#include <WiFi.h>
#include <time.h>
#include <math.h>

extern "C" const lv_font_t fd_sans_bold_10_ui;

namespace GiftUI {
#ifdef GIFT_HOST_TEST
bool hostAppBack();
#endif
namespace {
lv_obj_t *screen=nullptr,*battery=nullptr,*hours=nullptr,*minutes=nullptr,*homeDate=nullptr,*weekday=nullptr,*city=nullptr,*temp=nullptr,*condition=nullptr,*weekDays[3]={},*monthTitle=nullptr,*monthGrid=nullptr;
lv_obj_t *holidayAnswer=nullptr,*holidayDay=nullptr,*holidayNew=nullptr,*holidaySpring=nullptr;
lv_obj_t *waterLabel=nullptr,*knockLabel=nullptr,*moodSummary=nullptr,*weekMood=nullptr,*waterProgress=nullptr;
lv_obj_t *levels[2]={},*levelLabels[2]={},*clockTime=nullptr,*clockDate=nullptr,*clockImage=nullptr,*clockPeriod=nullptr,*clockCountdown=nullptr,*miniDays[42]={};
lv_obj_t *wifiCross=nullptr;
lv_obj_t *voiceMessage=nullptr,*quote=nullptr,*poem=nullptr,*moodError=nullptr;
lv_obj_t *weatherIcon=nullptr,*weatherFeels=nullptr,*weatherRange=nullptr,*weatherExtra=nullptr,*weatherUpdated=nullptr,*metricValues[6]={},*forecastWeek[7]={},*forecastNumbers[7]={},*forecastIcons[7]={},*homeWeatherIcon=nullptr,*weekNames[3]={};
lv_obj_t *todayFace=nullptr,*todayPlus=nullptr,*weekFaces[7]={},*weekPluses[7]={},*waterWave=nullptr,*quoteDate=nullptr,*quoteAuthor=nullptr,*poemAuthor=nullptr,*readingSources[2]={};
lv_obj_t *temperatureUnit=nullptr;
lv_obj_t *wifiCaption=nullptr,*readingBody=nullptr;
lv_obj_t *woodFeedback=nullptr;uint32_t woodFeedbackUntil=0;
lv_obj_t *homeCondition=nullptr,*homeCity=nullptr;
// ---- local animation state ------------------------------------------------
// Only widgets created by the hand-written pages are animated; the generated
// designer pages stay static. All animations are cancelled in render() before
// the old page can be replaced (see cancelPageAnimations()).
lv_obj_t *woodArt=nullptr;uint16_t woodArtBase=256;
lv_obj_t *moodArts[6]={};
lv_obj_t *weatherSpinIcon=nullptr;bool weatherWasPending=false;
lv_coord_t woodFeedbackY=0,waterFromY=0,waterToY=0;
String section,moodDraft="calm",detailKind,openedDate,lastDay,drawnPreset,editorDate,readingRaw;
bool holding=false,savedPage=false;uint32_t lastRevision=0,lastRefresh=0;
// Which hand-written page module owns the current layer: 0 home, 1 weather,
// 3 reading, 4 daily; -1 = legacy generated page (or an app page). Set by
// render()/buildHome(); used to route refresh/back into the right module.
int activePageModule=-1;
uint8_t appsSheet=0;
uint8_t gamesSheet=0;
uint8_t clockSheet=0;
constexpr unsigned AppsLastSheet=2;
constexpr unsigned GamesPerSheet=6;
const char *week[]={"日","一","二","三","四","五","六"};
const char *english[]={"SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"};
const char *moodIds[]={"happy","calm","neutral","tired","anxious","sad"};
const char *moodNames[]={"开心","平静","一般","疲惫","焦虑","难过"};
enum Event { OpenAlarmApp=40, OpenPomodoroApp=41, OpenTetrisApp=42, OpenMusicApp=43, OpenTimerApp=44, OpenSnakeApp=45, OpenJumpApp=46, OpenSheepApp=47, OpenMinerApp=48, OpenPlaneApp=49, OpenBreakoutApp=50, OpenMinesApp=51, OpenPinApp=52, OpenMemoryApp=53, OpenDaysApp=54, OpenRadioApp=55, OpenGamesApp=56, GamesPrevious=57, GamesNext=58,
 HomeCalendar=100, HomeCountdown, OpenMood, SaveMood, CancelMood, Strike, ResetKnocks, Water200, Water100, UndoWater,
 MessageMood, MessageLetter, MessageKeepsake, MessageClose, MessageEdit, OpenClock, ClockLandscape, ClockMatrix, ClockFlip, ClockPicker, RetryReading, OpenWater, OpenWood, Water500, UndoStrike, ToggleWoodSound, OpenQuote, OpenPoem, HolidayNew, HolidaySpring,
 ClockDashboard, ClockMinimal, ClockCompact, ClockSettings, ClockBarbar, ClockPrevious, ClockNext };
uint32_t color(JsonVariantConst v,uint32_t fallback){const char *s=v|"";return strlen(s)==7&&s[0]=='#'?strtoul(s+1,nullptr,16):fallback;}
void text(lv_obj_t *object,const String &value){if(object&&String(lv_label_get_text(object))!=value)lv_label_set_text(object,value.c_str());}
void source(lv_obj_t *obj,const lv_img_dsc_t *value){if(obj&&lv_img_get_src(obj)!=value)lv_img_set_src(obj,value);}
void textColor(lv_obj_t *obj,uint32_t value){if(obj&&lv_obj_get_style_text_color(obj,0).full!=lv_color_hex(value).full)lv_obj_set_style_text_color(obj,lv_color_hex(value),0);}
void background(lv_obj_t *obj,uint32_t value){if(obj&&lv_obj_get_style_bg_color(obj,0).full!=lv_color_hex(value).full)lv_obj_set_style_bg_color(obj,lv_color_hex(value),0);}
void opacity(lv_obj_t *obj,lv_opa_t value){if(obj&&lv_obj_get_style_bg_opa(obj,0)!=value)lv_obj_set_style_bg_opa(obj,value,0);}
void hidden(lv_obj_t *obj,bool value){if(obj&&lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN)!=value){if(value)lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(obj,LV_OBJ_FLAG_HIDDEN);}}
void rerender(){renderPending=true;lastInputAt=millis();}
void event(lv_event_t *e);

// ---- local animations -----------------------------------------------------
// Every animation only touches the widget passed as `var`, so the dirty area is
// that widget (the table in opt-lvgl-report.md has the estimated pixel counts).
// Cancellation is centralised: desk.cpp cleans the old page *before* calling
// render(), so lv_anim_del() there receives stale pointers; it only compares the
// pointer value and never dereferences it.
void stopAnim(lv_obj_t *obj){if(obj)lv_anim_del(obj,nullptr);}

void popInExec(void *var,int32_t value){
 auto *img=(lv_obj_t*)var;
 lv_img_set_zoom(img,(uint16_t)(180+(44*(int)value)/255));
 lv_obj_set_style_img_opa(img,(lv_opa_t)value,0);
}
// Mood icons pop in (180 -> 224 zoom, transparent -> opaque). Both end exactly on
// the values the static page used, so the settled page is pixel-identical.
void popIn(lv_obj_t *img){
 if(!img)return;stopAnim(img);
 lv_img_set_zoom(img,180);lv_obj_set_style_img_opa(img,0,0);
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,img);lv_anim_set_values(&anim,0,255);
 lv_anim_set_time(&anim,260);lv_anim_set_path_cb(&anim,Ui::easeInOut());lv_anim_set_exec_cb(&anim,popInExec);lv_anim_start(&anim);
}

void knockExec(void *var,int32_t value){
 lv_img_set_zoom((lv_obj_t*)var,(uint16_t)((int)woodArtBase-40+(40*(int)value)/255));
}
// Wooden fish: the art is squashed 40/256ths down and eases back to its exact
// original zoom; only the image preview is invalidated.
void knockWood(){
 if(!woodArt)return;stopAnim(woodArt);lv_img_set_zoom(woodArt,(uint16_t)((int)woodArtBase-40));
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,woodArt);lv_anim_set_values(&anim,0,255);
 lv_anim_set_time(&anim,180);lv_anim_set_path_cb(&anim,Ui::easeInOut());lv_anim_set_exec_cb(&anim,knockExec);lv_anim_start(&anim);
}

void feedbackExec(void *var,int32_t value){
 auto *obj=(lv_obj_t*)var;
 lv_obj_set_y(obj,(lv_coord_t)(woodFeedbackY-(14*(int)value)/100));
 lv_obj_set_style_text_opa(obj,(lv_opa_t)(255-((255*(int)value)/100)),0);
}
void feedbackDone(lv_anim_t *anim){
 auto *obj=(lv_obj_t*)lv_anim_get_user_data(anim);if(!obj)return;
 lv_obj_add_flag(obj,LV_OBJ_FLAG_HIDDEN);
 lv_obj_set_y(obj,woodFeedbackY);lv_obj_set_style_text_opa(obj,255,0);
}
// "+1" hint: rises 14px while fading out, then returns to its original hidden
// state, so a static page is unchanged.
void showWoodFeedback(){
 if(!woodFeedback)return;stopAnim(woodFeedback);lv_obj_clear_flag(woodFeedback,LV_OBJ_FLAG_HIDDEN);
 woodFeedbackY=lv_obj_get_y(woodFeedback);
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,woodFeedback);lv_anim_set_values(&anim,0,100);
 lv_anim_set_time(&anim,650);lv_anim_set_path_cb(&anim,lv_anim_path_linear);lv_anim_set_exec_cb(&anim,feedbackExec);
 lv_anim_set_ready_cb(&anim,feedbackDone);lv_anim_set_user_data(&anim,woodFeedback);lv_anim_start(&anim);
}

void countdownExec(void *var,int32_t value){lv_obj_set_style_text_opa((lv_obj_t*)var,(lv_opa_t)value,0);}
// The countdown line fades in whenever its text changes; it ends fully opaque,
// which is the value it had before the animation existed.
void fadeCountdown(){
 if(!clockCountdown)return;stopAnim(clockCountdown);
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,clockCountdown);lv_anim_set_values(&anim,0,255);
 lv_anim_set_time(&anim,180);lv_anim_set_path_cb(&anim,Ui::easeInOut());lv_anim_set_exec_cb(&anim,countdownExec);lv_anim_start(&anim);
}

void waterExec(void *var,int32_t value){
 auto *obj=(lv_obj_t*)var;
 lv_obj_set_y(obj,(lv_coord_t)(waterFromY+((int32_t)(waterToY-waterFromY)*(int)value)/1000));
}
// Water level: the wave layer only moves vertically inside the water card.
void raiseWater(lv_coord_t target){
 if(!waterWave||lv_obj_get_y(waterWave)==target)return;
 stopAnim(waterWave);waterFromY=lv_obj_get_y(waterWave);waterToY=target;
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,waterWave);lv_anim_set_values(&anim,0,1000);
 lv_anim_set_time(&anim,300);lv_anim_set_path_cb(&anim,Ui::easeInOut());lv_anim_set_exec_cb(&anim,waterExec);lv_anim_start(&anim);
}

// Called at the top of render() (i.e. after desk.cpp cleaned the previous page
// and before any new widget exists). Frees every animation that still points at
// a freed widget.
void cancelPageAnimations(){
 Ui::cancelAnimations();
 stopAnim(woodArt);stopAnim(weatherSpinIcon);stopAnim(clockCountdown);stopAnim(woodFeedback);stopAnim(waterWave);
 for(auto *art:moodArts)stopAnim(art);
 woodArt=nullptr;weatherSpinIcon=nullptr;for(auto &art:moodArts)art=nullptr;
 // The hand-written pages own their own animation variables.
 HomePage::cancel();WeatherPage::cancel();DailyPage::cancel();ReadingPage::cancel();
 GifClockPage::cancel();PixelClockPage::cancel();FlipClockPage::cancel();BarbarClockPage::cancel();
 DashboardClockPage::cancel();DashboardClockSettings::cancel();
 FocusApps::cancel();
 TimerPage::cancel();DaysPage::cancel();
 TetrisPage::cancel();SnakePage::cancel();JumpPage::cancel();SheepPage::cancel();MinerPage::cancel();PlanePage::cancel();BreakoutPage::cancel();MinesPage::cancel();PinPage::cancel();MemoryPage::cancel();
 MusicPage::cancel();
 RadioPage::cancel();
}
lv_obj_t *label(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font=saved_gift_font_bold_10_0,uint32_t ink=Ui::Ink){if(ink==Ui::Ink)ink=color(GiftData::get("colors")["ink"],Ui::Ink);auto *obj=Ui::label(parent,value,x,y,w,font,ink);lv_obj_add_style(obj,&UiStyles::labelLine2,0);return obj;}
lv_obj_t *panel(lv_obj_t *parent,int x,int y,int w,int h,uint32_t bg,int radius=10){auto *p=Ui::panel(parent,x,y,w,h,bg,radius);lv_obj_clear_flag(p,LV_OBJ_FLAG_CLICKABLE);return p;}
lv_obj_t *button(lv_obj_t *parent,const String &value,int x,int y,int w,int h,int action,uint32_t bg=Ui::Green){if(bg==Ui::Green)bg=color(GiftData::get("colors")["header"],Ui::Green);auto *b=panel(parent,x,y,w,h,bg,6);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,event,LV_EVENT_CLICKED,(void*)(uintptr_t)action);Ui::attachPressFeedback(b);auto *caption=label(b,value,3,0,w-6);lv_obj_add_style(caption,&UiStyles::textCenter,0);lv_obj_align(caption,LV_ALIGN_CENTER,0,0);return b;}
lv_obj_t *hit(lv_obj_t *parent,int x,int y,int w,int h,int action){auto *b=button(parent,"",x,y,w,h,action);lv_obj_add_style(b,&UiStyles::bgTransparent,0);return b;}
lv_obj_t *image(lv_obj_t *parent,const lv_img_dsc_t *source,int x,int y){auto *img=lv_img_create(parent);lv_img_set_src(img,source);lv_obj_set_pos(img,x,y);return img;}
lv_obj_t *centred(lv_obj_t *parent,const String &value,int x,int y,int w,const lv_font_t *font,uint32_t ink){auto *obj=label(parent,value,x,y,w,font,ink);lv_obj_add_style(obj,&UiStyles::textCenter,0);return obj;}
lv_obj_t *plus(lv_obj_t *parent,int size,int thickness){auto *holder=panel(parent,0,0,size,size,0,0);lv_obj_add_style(holder,&UiStyles::bgTransparent,0);lv_obj_center(holder);auto *a=panel(holder,0,0,size,thickness,0xae8f76,1);auto *b=panel(holder,0,0,thickness,size,0xae8f76,1);lv_obj_center(a);lv_obj_center(b);return holder;}
void border(lv_obj_t *obj,int width,uint32_t ink){lv_obj_set_style_border_width(obj,width,0);lv_obj_set_style_border_color(obj,lv_color_hex(ink),0);}
const lv_img_dsc_t *symbol(int code,bool small=false){if(code<0)return small?&gift_forecast_unknown:&gift_weather_unknown;if(code==0)return small?&gift_forecast_sun:&gift_weather_sun;if(code<4)return small?&gift_forecast_cloud:&gift_weather_cloud;if(code<50)return small?&gift_forecast_fog:&gift_weather_fog;if(code<70||code==80||code==81||code==82)return small?&gift_forecast_rain:&gift_weather_rain;if(code<90)return small?&gift_forecast_snow:&gift_weather_snow;return small?&gift_forecast_storm:&gift_weather_storm;}
const char *weatherName(int code){if(code<0)return "暂无天气";if(code==0)return "晴";if(code==3)return "阴";if(code<4)return "多云";if(code<50)return "雾";if(code<70||code==80||code==81||code==82)return "雨";if(code<90)return "雪";return "雷雨";}
const lv_img_dsc_t *moodImage(const String &id,bool small=false){if(id=="happy")return small?&gift_mood_small_happy:&gift_mood_happy;if(id=="calm")return small?&gift_mood_small_calm:&gift_mood_calm;if(id=="neutral")return small?&gift_mood_small_neutral:&gift_mood_neutral;if(id=="tired")return small?&gift_mood_small_tired:&gift_mood_tired;if(id=="anxious")return small?&gift_mood_small_anxious:&gift_mood_anxious;return small?&gift_mood_small_sad:&gift_mood_sad;}
lv_obj_t *nav(const String &title,int titleX=54,int titleY=8,const lv_font_t *titleFont=nullptr,bool cluster=false,bool offlineIcon=false){auto *b=Ui::button(screen,"",6,3,36,27,Ui::Back);lv_obj_add_style(b,&UiStyles::bgTransparent,0);if(auto *bc=lv_obj_get_child(b,0)){lv_label_set_text(bc,LV_SYMBOL_LEFT);lv_obj_set_style_text_font(bc,&lv_font_montserrat_20,0);}auto *caption=label(screen,title,titleX,titleY,230,titleFont?titleFont:saved_gift_font_bold_14_0);panel(screen,0,30,320,1,0xdde5d8,0);if(cluster)Ui::statusCluster(offlineIcon);return caption;}
int dow(DeskLogic::Civil d){int64_t n=DeskLogic::civilDays(d.year,d.month,d.day);return (int)((n+4)%7+7)%7;}
int waterTotal(JsonVariantConst record){int total=0;for(JsonVariantConst item:record["water"].as<JsonArrayConst>())total+=item["amount"]|0;return total;}
String dateKey(DeskLogic::Civil d){char value[11];snprintf(value,sizeof(value),"%04d-%02d-%02d",d.year,d.month,d.day);return value;}
String moodName(const String &id){for(int i=0;i<6;i++)if(id==moodIds[i])return moodNames[i];return "未记录";}
String readingText(const char *kind){auto o=GiftData::get("reading");auto cache=GiftData::get("readingCache")[kind];bool isQuote=!strcmp(kind,"quote"),manual=o[isQuote?"quoteMode":"poemMode"]=="manual";String value=manual||cache.isNull()?String(o[isQuote?"quoteText":"poemText"]|""):String(cache["text"]|"");String author=manual||cache.isNull()?String(o[isQuote?"quoteAuthor":"poemAuthor"]|""):String(cache["author"]|"");return value+"\n\n"+author;}
String readingValue(const char *kind,const char *field){auto o=GiftData::get("reading"),cache=GiftData::get("readingCache")[kind];String key=String(kind)+field;bool manual=o[String(kind)+"Mode"]=="manual";String cacheKey=field;cacheKey[0]=tolower(cacheKey[0]);return manual||cache.isNull()?String(o[key]|""):String(cache[cacheKey]|"");}
String readingBodyText(const char *kind){
 String body=ChineseLayout::wrap(readingText(kind),saved_gift_yahei_b_16_1,286);
 String author=!strcmp(kind,"quote")?readingValue("quote","Author"):String(readingValue("poem","Dynasty"))+" · "+readingValue("poem","Author");
 if(author.isEmpty()||author==" · ")author="佚名";
 // some cached poems already end with the author name: do not repeat it
 String plainAuthor=!strcmp(kind,"quote")?readingValue("quote","Author"):readingValue("poem","Author");
 if(!plainAuthor.isEmpty()){
  int at=body.lastIndexOf(plainAuthor);
  if(at>=0&&body.length()-(at+plainAuthor.length())<=4)return body;
 }
 return body+"\n\n"+author;
}
void reading(){
 auto o=GiftData::get("reading");lv_obj_set_style_bg_color(screen,lv_color_hex(color(o["background"],0xa4ccac)),0);
 if(section=="reading-quote"||section=="reading-poem"){const char *kind=section=="reading-quote"?"quote":"poem";nav(!strcmp(kind,"quote")?"每日名言":"诗词全文",54,8,nullptr,true,!strcmp(kind,"quote"));auto *body=panel(screen,0,32,320,208,color(o["quoteColor"],0xffffff),0);lv_obj_add_flag(body,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(body,LV_DIR_VER);readingRaw=readingBodyText(kind);readingBody=label(body,readingRaw,14,7,290,saved_gift_yahei_b_16_1,color(o["quoteInk"],0x292929));lv_obj_set_height(readingBody,194);lv_label_set_long_mode(readingBody,LV_LABEL_LONG_WRAP);lv_obj_set_style_text_align(readingBody,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_style_text_line_space(readingBody,8,0);return;}
 auto *q=panel(screen,8,5,304,131,color(o["quoteColor"],0xffffff),13);auto *mark=label(q,"“",70,10,170,saved_gift_font_bold_155_0,0xf4f4f4);lv_obj_set_height(mark,105);quoteDate=centred(q,"--/--",99,4,106,saved_gift_font_bold_21_0,0xa6a6a6);
 auto *content=panel(q,15,37,274,61,color(o["quoteColor"],0xffffff),0);lv_obj_add_style(content,&UiStyles::bgTransparent,0);quote=centred(content,"",0,0,274,saved_gift_yahei_b_16_1,color(o["quoteInk"],0x292929));lv_obj_set_style_text_line_space(quote,5,0);lv_obj_set_height(quote,60);lv_label_set_long_mode(quote,LV_LABEL_LONG_DOT);hit(q,15,37,274,61,OpenQuote);
 auto *feather=image(q,&gift_quote_feather,98,108);lv_obj_set_style_img_recolor(feather,lv_color_hex(0x292929),0);lv_obj_add_style(feather,&UiStyles::iconTint,0);quoteAuthor=centred(q,"",115,108,123,saved_gift_font_bold_10_0,color(o["quoteInk"],0x292929));readingSources[0]=label(q,"示例",255,4,42,saved_gift_font_bold_8_0,0x7c7c7c);
 auto *p=panel(screen,27,140,266,91,color(o["poemColor"],0xb80e10),13);label(p,"“",14,-6,44,saved_gift_font_bold_64_0,0xca4444);poem=label(p,"",21,18,224,saved_gift_yahei_b_16_1,color(o["poemInk"],0xffffff));lv_obj_set_height(poem,53);lv_obj_set_style_text_line_space(poem,4,0);lv_label_set_long_mode(poem,LV_LABEL_LONG_DOT);hit(p,21,18,224,53,OpenPoem);poemAuthor=label(p,"",93,74,159,saved_gift_font_bold_10_0,color(o["poemInk"],0xffffff));lv_obj_set_style_text_align(poemAuthor,LV_TEXT_ALIGN_RIGHT,0);readingSources[1]=label(p,"示例",216,4,42,saved_gift_font_bold_8_0,0xe2acac);
}
void moodEditor(){
 lv_obj_set_style_bg_color(screen,lv_color_hex(color(GiftData::get("daily")["cardColor"],0xfff8dc)),0);nav("选择心情 · "+editorDate);
 for(int i=0;i<6;i++){int x=12+(i%3)*102,y=39+(i/3)*88;auto *b=button(screen,"",x,y,92,82,200+i,0xfff8dc);border(b,moodDraft==moodIds[i]?2:0,0xd5ad59);auto *art=image(b,moodImage(moodIds[i]),14,2);lv_img_set_pivot(art,0,0);lv_img_set_zoom(art,224);moodArts[i]=art;popIn(art);centred(b,moodNames[i],2,62,88,saved_gift_font_bold_14_0,0x775845);}
 moodError=centred(screen,"",8,219,304,saved_gift_font_bold_10_0,0xb14c34);
}
void woodPattern(lv_obj_t *wood){
 auto *pattern=panel(wood,0,0,100,35,0xed642d,0);
 lv_obj_add_event_cb(pattern,[](lv_event_t *e){lv_area_t area;lv_obj_get_coords(lv_event_get_target(e),&area);lv_draw_rect_dsc_t paint;lv_draw_rect_dsc_init(&paint);paint.bg_color=lv_color_hex(0xffbe79);paint.bg_opa=64;for(int x=-14;x<100;x+=27){lv_point_t points[]={{(lv_coord_t)(area.x1+x),(lv_coord_t)area.y1},{(lv_coord_t)(area.x1+x+27),(lv_coord_t)area.y1},{(lv_coord_t)(area.x1+x+14),(lv_coord_t)(area.y1+24)}};lv_draw_triangle(lv_event_get_draw_ctx(e),&paint,points);}},LV_EVENT_DRAW_MAIN,nullptr);
}
void woodMallet(lv_obj_t *wood){
 static const lv_point_t shaft[]={{0,0},{42,15}};auto *line=lv_line_create(wood);lv_line_set_points(line,shaft,2);lv_obj_set_pos(line,59,27);lv_obj_set_style_line_width(line,4,0);lv_obj_set_style_line_color(line,lv_color_hex(0xd3954e),0);lv_obj_set_style_line_rounded(line,true,0);
 auto *head=panel(wood,54,23,12,12,0xc5a15c,LV_RADIUS_CIRCLE);panel(head,1,1,8,8,0xffe8a4,LV_RADIUS_CIRCLE);panel(head,2,2,4,4,0xfffbd0,LV_RADIUS_CIRCLE);
 woodFeedback=label(wood,"+1",71,65,26,saved_gift_font_bold_14_0,0xfff8cc);lv_obj_add_flag(woodFeedback,LV_OBJ_FLAG_HIDDEN);
}
void daily(){
 if(section=="mood"){moodEditor();return;}
 auto options=GiftData::get("daily");uint32_t bg=color(options["background"],0xa4ccac),card=color(options["cardColor"],0xfff8dc);lv_obj_set_style_bg_color(screen,lv_color_hex(bg),0);
 if(section=="water"||section=="wood"){
  lv_obj_set_style_bg_color(screen,lv_color_hex(card),0);nav(section=="water"?"喝水记录":"敲木鱼");
  if(section=="water"){waterLabel=label(screen,"",14,40,290,saved_gift_font_bold_14_0,0x775845);button(screen,"+200 ml",12,68,91,30,Water200);button(screen,"+500 ml",112,68,91,30,Water500);button(screen,"撤销上次",212,68,96,30,UndoWater);auto *log=panel(screen,12,108,296,125,0xfffdf3,6);lv_obj_add_flag(log,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(log,LV_DIR_VER);auto record=GiftData::record(GiftData::today());int row=0;for(JsonVariantConst item:record["water"].as<JsonArrayConst>())label(log,TextUtil::formatDate((item["at"]|0LL)/1000,"%H:%M")+"   +"+String(item["amount"]|0)+" ml",10,6+row++*22,277,saved_gift_font_bold_14_0,0x775845);if(!row)label(log,"今天还没有饮水记录。",10,8,277,saved_gift_font_bold_14_0,0x775845);}
  else{knockLabel=label(screen,"",14,41,292,saved_gift_font_bold_14_0,0x775845);auto *wood=button(screen,"",90,72,140,91,Strike,0xed642d);auto *art=image(wood,&gift_wood_art,0,11);lv_img_set_pivot(art,0,0);lv_img_set_zoom(art,358);woodArt=art;woodArtBase=358;woodFeedback=label(screen,"+1",200,96,32,saved_gift_font_bold_14_0,0xfff8cc);lv_obj_add_flag(woodFeedback,LV_OBJ_FLAG_HIDDEN);button(screen,"撤销一次",12,177,139,30,UndoStrike);button(screen,(options["sound"]|true)?"声音：开":"声音：关",164,177,143,30,ToggleWoodSound);label(screen,"触摸木鱼可敲击，音量跟随设备设置。",12,216,298,saved_gift_font_bold_10_0,0x775845);}
  return;
 }
 auto *top=panel(screen,8,5,304,131,card,16);label(top,"今天的心情如何呢？",18,14,145,saved_gift_font_bold_10_0,0x775845);auto *today=button(top,"",36,42,67,68,OpenMood,card);lv_obj_set_style_radius(today,LV_RADIUS_CIRCLE,0);border(today,4,0xffdf6c);lv_obj_set_style_clip_corner(today,true,0);todayFace=image(today,&gift_mood_calm,0,0);lv_img_set_pivot(todayFace,0,0);lv_img_set_zoom(todayFace,236);todayPlus=plus(today,29,3);
 auto now=DeskLogic::beijingDate(ClockSource::now());int mondayOffset=(dow(now)+6)%7;
 for(int i=0;i<7;i++){int x=i<3?157+i*38:140+(i-3)*38,y=i<3?13:69;auto *day=button(top,"",x,y,31,51,280+i,card);auto *ring=panel(day,2,0,27,27,card,LV_RADIUS_CIRCLE);border(ring,2,0xffdf6c);weekFaces[i]=image(ring,&gift_mood_small_calm,-2,-2);weekPluses[i]=plus(ring,11,2);centred(day,"周"+String(week[(i+1)%7]),0,32,31,saved_gift_font_bold_10_0,0x775845);if(i>mondayOffset)lv_obj_add_state(day,LV_STATE_DISABLED);if(i==mondayOffset)panel(day,8,46,15,2,0xd5ad59,0);}
 auto *wood=button(screen,"",8,140,100,91,Strike,0xed642d);lv_obj_set_style_radius(wood,11,0);lv_obj_set_style_clip_corner(wood,true,0);woodPattern(wood);image(wood,&gift_wood_art,0,34);woodMallet(wood);label(wood,"今日敲击",10,10,68,saved_gift_font_bold_8_0,0xfff1c7);knockLabel=label(wood,"0 次",10,23,70,saved_gift_font_bold_14_0,0xfff1c7);auto *more=button(wood,"更多",71,11,26,15,OpenWood,0xed642d);lv_obj_set_style_text_font(lv_obj_get_child(more,0),saved_gift_font_bold_8_0,0);lv_obj_set_style_text_color(lv_obj_get_child(more,0),lv_color_hex(0xfff1c7),0);
 image(screen,&gift_daily_cat,111,140);
 auto *water=panel(screen,213,140,99,91,0xacd5fc,12);lv_obj_set_style_clip_corner(water,true,0);waterWave=panel(water,-12,91,128,96,0x80c0f7,60);auto *summary=button(water,"",8,6,85,43,OpenWater,0xacd5fc);lv_obj_add_style(summary,&UiStyles::bgTransparent,0);waterLabel=label(summary,"0ml",0,0,86,&lv_font_montserrat_20,0x262443);waterProgress=label(summary,"0% · 1000ml",0,25,86,saved_gift_font_bold_8_0,0xffffff);
 for(int i=0;i<2;i++){auto *b=button(water,"",10+i*43,54,36,30,i==0?Water200:Water500,0xffffff);image(b,&gift_water_cup,10,2);centred(b,i==0?"200ml":"500ml",0,18,36,saved_gift_font_bold_8_0,0x329ede);}
}
void levelEvent(lv_event_t *e){
 if(lv_event_get_code(e)==LV_EVENT_PRESSED)holding=true;
 if(lv_event_get_code(e)==LV_EVENT_RELEASED||lv_event_get_code(e)==LV_EVENT_PRESS_LOST){
  holding=false;
  int i=(uintptr_t)lv_event_get_user_data(e),value=lv_slider_get_value(lv_event_get_target(e));
  // The physical slider cannot be dragged past the panel edge; snap the last few
  // percent to 100 (and the first few to the minimum) so 100% is reachable.
  int snapped=i==0?(value>=95?100:value<=12?10:value):(value>=97?100:value<=3?0:value);
  if(snapped!=value){lv_slider_set_value(lv_event_get_target(e),snapped,LV_ANIM_OFF);GiftData::setLevel(i==0?"brightness":"volume",snapped);AudioBoard::setVolume(GiftData::get("deviceSettings")["volume"]|45);}
  lastRefresh=0;
 }
 if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED){int i=(uintptr_t)lv_event_get_user_data(e);GiftData::setLevel(i==0?"brightness":"volume",lv_slider_get_value(lv_event_get_target(e)));AudioBoard::setVolume(GiftData::get("deviceSettings")["volume"]|45);lastInputAt=millis();lastRefresh=0;}
}
// ---- settings page icons -------------------------------------------------
// The locked settings design draws its menu icons instead of using a font glyph:
// phone outline, info circle and colour palette. They are tiny primitives, so
// drawing them here keeps the rows pixel-consistent with the design.
lv_obj_t *outline(lv_obj_t *parent,int x,int y,int w,int h,int radius,int border,uint32_t color){
 auto *o=panel(parent,x,y,w,h,0xFFFFFF,radius);
 lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);
 lv_obj_set_style_border_width(o,border,0);
 lv_obj_set_style_border_color(o,lv_color_hex(color),0);
 lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);
 return o;
}
void linePrim(lv_obj_t *parent,const lv_point_t *points,int count,int x,int y,int width,uint32_t color){
 auto *l=lv_line_create(parent);
 lv_line_set_points(l,points,count);
 lv_obj_set_pos(l,x,y);
 lv_obj_set_style_line_width(l,width,0);
 lv_obj_set_style_line_color(l,lv_color_hex(color),0);
 lv_obj_set_style_line_rounded(l,true,0);
 lv_obj_clear_flag(l,LV_OBJ_FLAG_CLICKABLE);
}
void settingsIcon(lv_obj_t *row,int index){
 const uint32_t ink=0x394637;
 if(index==0){ // Wi-Fi: the locked design uses the same full 17x17 asset as the status bar
  auto *img=lv_img_create(row);lv_img_set_src(img,&ui_status_wifi);
  lv_obj_set_pos(img,16,5);lv_obj_clear_flag(img,LV_OBJ_FLAG_CLICKABLE);
 }else if(index==1){ // phone outline + speaker/screen lines
  outline(row,18,3,12,20,2,2,ink);
  static const lv_point_t top[]={{0,0},{6,0}},bottom[]={{0,0},{6,0}};
  linePrim(row,top,2,21,8,1,ink);
  linePrim(row,bottom,2,21,19,1,ink);
 }else if(index==2){ // info circle
  outline(row,16,4,17,17,LV_RADIUS_CIRCLE,2,ink);
  panel(row,23,8,3,3,ink,LV_RADIUS_CIRCLE);
  panel(row,23,13,2,5,ink,1);
 }else{ // colour palette
  outline(row,16,4,17,17,LV_RADIUS_CIRCLE,2,ink);
  const uint32_t dots[4]={0xDC7878,0x6EA0D2,0xDCB45A,0x6EAA78};
  const int dx[4]={20,26,20,26},dy[4]={8,7,15,14};
  for(int i=0;i<4;i++)panel(row,dx[i],dy[i],3,3,dots[i],LV_RADIUS_CIRCLE);
 }
}
void rightChevron(lv_obj_t *parent,int x,int y){
 static const lv_point_t points[]={{0,0},{5,5},{0,10}};
 linePrim(parent,points,3,x,y,2,0x82927A);
}
void settingsPage(){
 lv_obj_set_style_bg_color(screen,lv_color_hex(0xFBF9F1),0);
 lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
 // Locked design: thin drawn chevron @(12,14), title @(30,9) Bold15 (the 16 px
 // bold face is the closest available size), shared status cluster.
 Ui::compactHeader("设置");
 panel(screen,16,36,288,1,0xE0E5D8,0);
 uint32_t accent=color(GiftData::get("colors")["accent"],0x619b78);
 for(int i=0;i<2;i++){
  const int y=42+i*44;
  label(screen,i==0?"屏幕亮度":"音量",16,y,150,&fd_sans_bold_12,0x394637);
  levelLabels[i]=label(screen,"",246,y,58,saved_gift_font_bold_11_0,accent);
  lv_obj_set_style_text_align(levelLabels[i],LV_TEXT_ALIGN_RIGHT,0);
  levels[i]=lv_slider_create(screen);
  lv_obj_set_pos(levels[i],16,y+18);
  lv_obj_set_size(levels[i],288,7);
  lv_slider_set_range(levels[i],i==0?10:0,100);
  lv_slider_set_value(levels[i],GiftData::get("deviceSettings")[i==0?"brightness":"volume"]|45,LV_ANIM_OFF);
  lv_obj_add_style(levels[i],&UiStyles::sliderTrack,LV_PART_MAIN);
  lv_obj_set_style_bg_color(levels[i],lv_color_hex(accent),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(levels[i],lv_color_hex(accent),LV_PART_KNOB);
  lv_obj_add_style(levels[i],&UiStyles::sliderKnob,LV_PART_KNOB);
  lv_obj_add_event_cb(levels[i],levelEvent,LV_EVENT_ALL,(void*)(uintptr_t)i);
 }
 const char *titles[]={"Wi-Fi 配网","连接手机","关于","颜色与触摸"};
 const Ui::Action actions[]={Ui::OpenWifi,Ui::OpenPhone,Ui::OpenAbout,Ui::OpenDiagnostics};
 for(int i=0;i<4;i++){
  const int y=126+i*27;
  auto *row=button(screen,"",0,y,320,27,actions[i],0xFBF9F1);
  lv_obj_add_style(row,&UiStyles::bgTransparent,0);
  settingsIcon(row,i);
  label(row,titles[i],44,7,220,&fd_sans_bold_12,0x394637);
  rightChevron(row,292,9);
  panel(screen,16,y+27,288,1,0xE6E9DF,0);
 }
}
// ---- clock faces ---------------------------------------------------------
void clockPage(){
  if(section=="clock-settings"){DashboardClockSettings::build(screen);return;}
  if(section!="clock-face"){
  nav("时钟模式",50,8,saved_gift_font_bold_14_0,true,false);
  const char *names[]={"时光动画","Pixel Clock","白色翻页","天气大字钟","纯时钟","紧凑看板","Barbar 彩条钟"};
  const char *ids[]={"landscape","matrix","flip","dashboard","minimal","compact","barbar"};
  const char *hints[]={"单击播放 双击切换","像素方块与小蛇","双击切换明暗","日期天气 弹性滚动","大字时间 轻盈摆动","左侧时间 右侧天气","双击显示/隐藏秒数"};
  const int actions[]={ClockLandscape,ClockMatrix,ClockFlip,ClockDashboard,ClockMinimal,ClockCompact,ClockBarbar};
  String current=GiftData::get("clockMode")["preset"]|"landscape";
  for(int i=clockSheet*6;i<7&&i<(clockSheet+1)*6;i++){
   const int slot=i%6;
   auto *card=button(screen,"",9+(slot%3)*103,39+(slot/3)*87,96,82,actions[i],0xffffff);
   lv_obj_set_style_radius(card,9,0);
   border(card,current==ids[i]?2:1,current==ids[i]?0x619b78:0xcad7c0);
   auto *thumb=panel(card,6,5,84,44,0xffffff,5);
   lv_obj_set_style_clip_corner(thumb,true,0);
   lv_obj_set_style_pad_all(thumb,0,0);
   if(i==0){
    panel(thumb,0,0,84,44,0x000000,0);
    auto *t=label(thumb,"06:28",0,2,84,saved_gift_yahei_b_16_1,0xffffff);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);
    for(int eye=0;eye<2;eye++){
     panel(thumb,23+eye*22,22,18,18,0xffffff,LV_RADIUS_CIRCLE);
     panel(thumb,28+eye*22,25,10,12,0x000000,LV_RADIUS_CIRCLE);
    }
    panel(thumb,40,38,5,3,0x8bd2ed,2);
   }else if(i==1){
    auto *preview=panel(thumb,0,0,84,44,0x090a0d,0);
    // Draw the miniature in one widget; 171 decorative objects would waste the
    // internal heap just to show the picker thumbnail.
    lv_obj_add_event_cb(preview,[](lv_event_t *e){
     const char *glyphs[]={"010110010010111","111100111001111","111001111100111","111001111001111"};
     const int starts[]={1,5,11,15};lv_area_t origin;lv_obj_get_coords(lv_event_get_target(e),&origin);
     lv_draw_rect_dsc_t ink;lv_draw_rect_dsc_init(&ink);ink.bg_opa=LV_OPA_COVER;
     for(int r=0;r<9;r++)for(int c=0;c<19;c++){
      bool on=c==9&&(r==3||r==5);
      for(int d=0;d<4;d++)if(r>=2&&r<7&&c>=starts[d]&&c<starts[d]+3)on=glyphs[d][(r-2)*3+c-starts[d]]=='1';
      ink.bg_color=lv_color_hex(on?0xf5f5f2:0x262930);
      lv_coord_t x=origin.x1+4+c*4,y=origin.y1+4+r*4;lv_area_t dot={x,y,(lv_coord_t)(x+2),(lv_coord_t)(y+2)};
      lv_draw_rect(lv_event_get_draw_ctx(e),&ink,&dot);
     }
    },LV_EVENT_DRAW_MAIN,nullptr);
   }else if(i==2){
    const bool dark=GiftData::get("clockMode")["flipDark"]|false;
    const uint32_t background=dark?0x101216:0xf7f7f2,paper=dark?0x252a32:0xffffff;
    const uint32_t ink=dark?0xf0f3f7:0x171717,hinge=dark?0x11151b:0xdcdcd8;
    panel(thumb,0,0,84,44,background,0);
    panel(thumb,5,4,35,30,paper,4);panel(thumb,44,4,35,30,paper,4);
    auto *d0=label(thumb,"16",5,7,35,saved_gift_font_bold_20_0,ink);lv_obj_set_style_text_align(d0,LV_TEXT_ALIGN_CENTER,0);
    auto *d1=label(thumb,"53",44,7,35,saved_gift_font_bold_20_0,ink);lv_obj_set_style_text_align(d1,LV_TEXT_ALIGN_CENTER,0);
    panel(thumb,5,18,35,1,hinge,0);panel(thumb,44,18,35,1,hinge,0);
    auto *w=label(thumb,"星期五",0,35,84,saved_gift_font_bold_7_0,dark?0xb0bac8:0x6b6b63);lv_obj_set_style_text_align(w,LV_TEXT_ALIGN_CENTER,0);
   }else if(i==6){
    BarbarClockPage::createPreview(thumb);
   }else{
    panel(thumb,0,0,84,44,0x000000,0);
    if(i==3){
     centred(thumb,"13  星期五",0,2,84,saved_gift_font_bold_7_0,0xc7cbd3);
     centred(thumb,"10:24",0,8,84,&fd_sans_bold_26,0x659fe8);
     centred(thumb,"晴  18°C",0,35,84,saved_gift_font_bold_7_0,0xa4adb8);
    }else if(i==4){
     centred(thumb,"10:24",0,7,84,&fd_sans_bold_26,0xffffff);
    }else{
     centred(thumb,"10:24",1,13,53,saved_gift_font_bold_14_0,0x659fe8);
     centred(thumb,"13",56,3,26,saved_gift_font_bold_14_0,0xffffff);
     centred(thumb,"周五",56,21,26,saved_gift_font_bold_7_0,0xc7cbd3);
     centred(thumb,"18°C",54,31,30,saved_gift_font_bold_7_0,0xa4adb8);
    }
   }
   auto *name=label(card,names[i],0,51,96,i==6?&fd_sans_bold_12_ui:&fd_sans_bold_12,0x244432);lv_obj_set_style_text_align(name,LV_TEXT_ALIGN_CENTER,0);
   auto *hint=label(card,hints[i],2,68,92,i==6?&fd_sans_bold_10_ui:&fd_sans_bold_10,0x52674f);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
  }
  auto *previous=button(screen,"<",9,214,35,24,ClockPrevious,0xdce8d4);
  auto *settingsButton=button(screen,"钟面设置",51,214,218,24,ClockSettings,0xdce8d4);
  auto *next=button(screen,">",276,214,35,24,ClockNext,0xdce8d4);
  if(clockSheet==0){lv_obj_add_state(previous,LV_STATE_DISABLED);lv_obj_set_style_opa(previous,100,0);}
  if(clockSheet==1){lv_obj_add_state(next,LV_STATE_DISABLED);lv_obj_set_style_opa(next,100,0);}
  if(auto *caption=lv_obj_get_child(settingsButton,0)){lv_obj_set_style_text_font(caption,&fd_sans_bold_12,0);lv_obj_align(caption,LV_ALIGN_CENTER,0,0);}
  return;
 }
  String preset=GiftData::get("clockMode")["preset"]|"landscape";
  drawnPreset=preset;
  const bool flipDark=GiftData::get("clockMode")["flipDark"]|false;
  lv_obj_set_style_bg_color(screen,lv_color_hex(preset=="matrix"?0x050b10:preset=="flip"?(flipDark?0x101216:0xfaf9f7):0x000000),0);
  if(preset=="dashboard"||preset=="minimal"||preset=="compact")DashboardClockPage::build(screen,preset.c_str());
  else if(preset=="barbar")BarbarClockPage::build(screen);
  else if(preset=="flip")FlipClockPage::build(screen);
  else if(preset=="matrix")PixelClockPage::build(screen);
  else GifClockPage::build(screen);
}
void updateCountdown(bool valid,DeskLogic::Civil d){
  if(!clockCountdown)return;
  bool show=valid&&settings.hasTarget;
  bool wasHidden=lv_obj_has_flag(clockCountdown,LV_OBJ_FLAG_HIDDEN);
  hidden(clockCountdown,!show);if(!show)return;
  auto target=DeskLogic::beijingDate(settings.target);
  int64_t diff=DeskLogic::civilDays(target.year,target.month,target.day)-DeskLogic::civilDays(d.year,d.month,d.day);
  String name=settings.countdownName.isEmpty()?"期待的日子":settings.countdownName;
  String value;
  if(diff>0)value="距 "+name+" 还有 "+String((long)diff)+" 天";
  else if(diff==0)value="今天是 "+name;
  else value=name+" 已过去 "+String((long)-diff)+" 天";
  if(wasHidden||String(lv_label_get_text(clockCountdown))!=value)fadeCountdown();
  text(clockCountdown,value);
}
void voice(){
 nav("小智 AI");voiceMessage=label(screen,XiaozhiVoice::get().message,18,80,284,saved_gift_font_bold_14_0,0x527367);
}
unsigned savedIndex=0;
lv_obj_t *savedFind(const char *name){return SavedDesign::find(screen,name);}
void savedText(const char *name,const String &value){text(savedFind(name),value);}
void savedSource(const char *name,const lv_img_dsc_t *image){auto *obj=savedFind(name);if(!obj||!image)return;lv_obj_set_style_bg_img_src(obj,nullptr,0);lv_img_set_src(obj,image);int w=lv_obj_get_width(obj),h=lv_obj_get_height(obj);if(image->header.w&&image->header.h){int zw=w*256/image->header.w,zh=h*256/image->header.h;int zoom=zw<zh?zw:zh;lv_img_set_pivot(obj,0,0);lv_img_set_zoom(obj,(uint16_t)zoom);}}
void savedBind(const char *name,unsigned action){SavedDesign::bind(screen,name,event,action);}
void appTile(lv_obj_t *parent, int x, int y, const char *name, int action,
             const lv_img_dsc_t *art, const char *symbol = nullptr) {
 auto *tile=Ui::panel(parent,x,y,74,82,0xe7eee0,8);
 lv_obj_set_style_border_width(tile,1,0);lv_obj_set_style_border_color(tile,lv_color_hex(0x8ca48a),0);
 lv_obj_add_flag(tile,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(tile,event,LV_EVENT_CLICKED,(void*)(uintptr_t)action);Ui::attachPressFeedback(tile);
 if(art){auto *icon=lv_img_create(tile);lv_img_set_src(icon,art);lv_img_set_zoom(icon,256);lv_obj_set_pos(icon,9,3);lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE);}
 else {auto *icon=lv_label_create(tile);lv_label_set_text(icon,symbol);lv_obj_set_style_text_font(icon,&lv_font_montserrat_20,0);lv_obj_set_style_text_color(icon,lv_color_hex(action==OpenAlarmApp?0x527d59:0xb57935),0);lv_obj_align(icon,LV_ALIGN_TOP_MID,0,8);}
 auto *caption=Ui::label(tile,name,2,54,70,saved_gift_font_bold_10_0,0x294c3d);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);lv_obj_add_style(caption,&UiStyles::labelLine2,0);
}
struct GameEntry { const char *name; const char *id; int action; };
const GameEntry games[]={
 {"俄罗斯方块","game_tetris",OpenTetrisApp},{"贪吃蛇","game_snake",OpenSnakeApp},
 {"跳一跳","game_jump",OpenJumpApp},{"羊了个羊","game_sheep",OpenSheepApp},
 {"黄金矿工","game_miner",OpenMinerApp},{"飞机大战","game_plane",OpenPlaneApp},
 {"打砖块","game_breakout",OpenBreakoutApp},{"扫雷","game_mines",OpenMinesApp},
 {"见缝插针","game_pin",OpenPinApp},{"记忆翻牌","game_memory",OpenMemoryApp}
};
constexpr unsigned GameCount=sizeof(games)/sizeof(games[0]);
constexpr unsigned GamesLastSheet=(GameCount-1)/GamesPerSheet;
void gamesIcon(lv_obj_t *parent,int x,int y){
 auto *icon=panel(parent,x,y,56,56,0xDBE8CA,14);
 auto *body=panel(icon,5,15,46,29,0x315C43,11);
 panel(body,7,9,15,5,0xFBF6E9,1);panel(body,12,4,5,15,0xFBF6E9,1);
 panel(body,29,13,7,7,0xE6B96E,LV_RADIUS_CIRCLE);panel(body,36,6,7,7,0xFBF6E9,LV_RADIUS_CIRCLE);
 panel(icon,24,10,8,3,0x315C43,1);
}
void launcherIcon(lv_obj_t *tile,int action,const lv_img_dsc_t *art,int y){
 if(art){auto *icon=lv_img_create(tile);lv_img_set_src(icon,art);lv_obj_set_pos(icon,12,y);lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE);}
 else if(action==OpenGamesApp)gamesIcon(tile,12,y);
 else if(action==OpenSnakeApp)SnakePage::createIcon(tile,12,y);
 else if(action==OpenJumpApp)JumpPage::createIcon(tile,12,y);
 else if(action==OpenSheepApp)SheepPage::createIcon(tile,12,y);
 else if(action==OpenMinerApp)MinerPage::createIcon(tile,12,y);
 else if(action==OpenPlaneApp)PlanePage::createIcon(tile,12,y);
 else if(action==OpenBreakoutApp)BreakoutPage::createIcon(tile,12,y);
 else if(action==OpenMinesApp)MinesPage::createIcon(tile,12,y);
 else if(action==OpenPinApp)PinPage::createIcon(tile,12,y);
 else if(action==OpenMemoryApp)MemoryPage::createIcon(tile,12,y);
 else if(action==OpenDaysApp)DaysPage::createIcon(tile,12,y);
 else if(action==OpenRadioApp)RadioPage::createIcon(tile,12,y);
 else {
  auto *icon=panel(tile,12,y,56,56,0xDBE8CA,14);
  static const lv_point_t hourglass[]={{0,0},{28,0},{28,7},{0,31},{0,38},{28,38},{28,31},{0,7},{0,0}};
  auto *shape=lv_line_create(icon);lv_line_set_points(shape,hourglass,9);lv_obj_set_pos(shape,14,9);
  lv_obj_set_style_line_width(shape,3,0);lv_obj_set_style_line_color(shape,lv_color_hex(0x315C43),0);lv_obj_set_style_line_rounded(shape,true,0);
 }
}
void launcherTile(unsigned slot,const char *name,int action,const lv_img_dsc_t *art,bool game=false,const char *id=nullptr){
 auto *tile=lv_obj_create(screen);lv_obj_remove_style_all(tile);
 lv_obj_set_pos(tile,20+(slot%3)*100,game?34+(slot/3)*91:16+(slot/3)*109);lv_obj_set_size(tile,80,game?88:95);
 if(id)saved_set_name(tile,id);
 lv_obj_clear_flag(tile,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(tile,LV_OBJ_FLAG_CLICKABLE);
 lv_obj_add_event_cb(tile,event,LV_EVENT_CLICKED,(void*)(uintptr_t)action);
 launcherIcon(tile,action,art,game?2:8);
 auto *caption=lv_label_create(tile);lv_obj_remove_style_all(caption);lv_obj_set_pos(caption,0,game?61:71);lv_obj_set_size(caption,80,24);
 lv_obj_set_style_text_font(caption,(game||action>=OpenSnakeApp)?&fd_sans_bold_14_ui:saved_gift_font_bold_14_0,0);
 lv_obj_set_style_text_color(caption,lv_color_hex(0xFBF6E9),0);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
 lv_label_set_long_mode(caption,LV_LABEL_LONG_CLIP);lv_label_set_text(caption,name);lv_obj_clear_flag(caption,LV_OBJ_FLAG_CLICKABLE);
}
void launcherBackground(){
 auto *bg=lv_img_create(screen);lv_img_set_src(bg,&gift_apps_bg);lv_obj_set_pos(bg,0,0);lv_obj_clear_flag(bg,LV_OBJ_FLAG_CLICKABLE);
}
void gamesPage(){
 launcherBackground();panel(screen,0,0,320,32,0xFCF7ED,0);nav("游戏",50,6,&fd_sans_bold_16_ui);
 auto *number=centred(screen,String(gamesSheet+1)+" / "+String(GamesLastSheet+1),153,8,60,&lv_font_montserrat_14,0x315C43);
 lv_obj_set_height(number,20);
 auto *previous=hit(screen,219,0,44,32,GamesPrevious),*next=hit(screen,266,0,44,32,GamesNext);
 saved_set_name(previous,"games_previous");saved_set_name(next,"games_next");
 for(unsigned i=0;i<2;++i){
  auto *arrow=label(i?next:previous,i?LV_SYMBOL_RIGHT:LV_SYMBOL_LEFT,0,0,44,&lv_font_montserrat_20,0x315C43);
  lv_obj_set_style_text_align(arrow,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(arrow);
 }
 if(gamesSheet==0){lv_obj_add_state(previous,LV_STATE_DISABLED);lv_obj_set_style_opa(previous,80,0);}
 if(gamesSheet==GamesLastSheet){lv_obj_add_state(next,LV_STATE_DISABLED);lv_obj_set_style_opa(next,80,0);}
 const unsigned first=gamesSheet*GamesPerSheet;
 for(unsigned i=first;i<GameCount&&i<first+GamesPerSheet;++i)
  launcherTile(i-first,games[i].name,games[i].action,games[i].action==OpenTetrisApp?&tetris_app_icon:nullptr,true,games[i].id);
}
void appsPage(){
 if(appsSheet==0){
  SavedDesign::create(screen,6);
  savedBind("app_clock",OpenClock);savedBind("app_letter",1005);savedBind("app_weather",Ui::OpenWeather);
  savedBind("app_voice",Ui::OpenVoice);savedBind("app_calendar",HomeCalendar);savedBind("app_settings",Ui::OpenSettings);
  for(const char *name:{"image_1_copy","image_2_copy","image_3_copy","label_3_copy","label_4_copy","label_6_copy"})hidden(savedFind(name),true);
  if(auto *caption=savedFind("text_17"))lv_obj_set_y(caption,60);
  for(const char *name:{"text_2","text_7","text_12","text_17","text_22","text_27"})if(auto *caption=savedFind(name)){
   lv_obj_set_style_text_font(caption,saved_gift_font_bold_14_0,0);
   lv_obj_set_style_text_color(caption,lv_color_hex(0xFBF6E9),0);
   lv_obj_set_style_bg_opa(caption,LV_OPA_TRANSP,0);
  }
  SavedDesign::invalidate();
 }else{
  launcherBackground();
   const lv_img_dsc_t *icons[]={&focus_app_alarm,&focus_app_pomodoro,&music_app_icon,nullptr,nullptr,nullptr,nullptr};
   const char *names[]={"闹钟","番茄时钟","音乐","游戏","倒计时","倒数日","在线 FM"};const int actions[]={OpenAlarmApp,OpenPomodoroApp,OpenMusicApp,OpenGamesApp,OpenTimerApp,OpenDaysApp,OpenRadioApp};
   const unsigned first=(appsSheet-1)*6, count=sizeof(actions)/sizeof(actions[0]);
   for(unsigned i=first;i<count&&i<first+6;++i){
   launcherTile(i-first,names[i],actions[i],icons[i],false,actions[i]==OpenGamesApp?"app_games":nullptr);
  }
 }
}

const lv_img_dsc_t *savedWeatherImage(int code,bool forecast=false){
 if(forecast){if(code<0)return saved_gift_forecast_unknown;if(code==0)return saved_gift_forecast_sun;if(code<=3)return saved_gift_forecast_cloud;if(code<=48)return saved_gift_forecast_fog;if(code>=95)return saved_gift_forecast_storm;if(code>=71&&code<=86)return saved_gift_forecast_snow;return saved_gift_forecast_rain;}
 if(code<0)return saved_gift_weather_unknown;if(code==0)return saved_gift_weather_sun;if(code<=3)return saved_gift_weather_cloud;if(code<=48)return saved_gift_weather_fog;if(code>=95)return saved_gift_weather_storm;if(code>=71&&code<=86)return saved_gift_weather_snow;return saved_gift_weather_rain;
}
const lv_img_dsc_t *savedMoodImage(const String &mood,bool small=false){
 if(small){if(mood=="happy")return saved_gift_mood_happy_27;if(mood=="neutral")return saved_gift_mood_neutral_27;return mood=="calm"?saved_gift_mood_calm_27:mood=="tired"?saved_gift_mood_tired_27:mood=="anxious"?saved_gift_mood_anxious_27:saved_gift_mood_sad_27;}
 if(mood=="happy")return saved_gift_mood_happy;if(mood=="neutral")return saved_gift_mood_neutral;
 if(mood=="tired")return saved_gift_mood_tired;if(mood=="anxious")return saved_gift_mood_anxious;
 if(mood=="sad")return saved_gift_mood_sad;return saved_gift_mood_calm;
}
// Visual-first pass: the GUI Guider export is shown exactly as designed;
// the data-refresh fitting logic will be adapted in the next pass.
static const bool kDesignStatic=true;

void savedCreate(unsigned index){
 savedPage=true;savedIndex=index;SavedDesign::create(screen,index);
 for(unsigned i=0;i<7;i++){char name[16];snprintf(name,sizeof(name),"nav_%u",i);if(auto *nav=savedFind(name))hidden(nav,true);}
 if(index==0){
  savedBind("clock_card",OpenClock);savedBind("weather_card",Ui::OpenWeather);savedBind("week_card",HomeCountdown);savedBind("calendar_card",HomeCalendar);
  if(!kDesignStatic){
  // The condition/city labels used to sit inside 18px/30px wrappers and under
  // the full-card illustration. Replace them with card-level labels so long
  // Chinese conditions and city names are readable and always painted on top.
  if(auto *card=savedFind("weather_card")){
   // The 98x65 illustration exactly fills the card's content box. LVGL's
   // overflow flag on the image let its square bottom-left corner paint over the
   // card's 15px rounded corner; inset it two pixels and clear the flag so the
   // card's rounded silhouette stays intact.
   for(const char *name:{"art_12","weather_shade"})if(auto *obj=savedFind(name)){lv_obj_clear_flag(obj,LV_OBJ_FLAG_OVERFLOW_VISIBLE);lv_obj_set_pos(obj,2,2);lv_obj_set_size(obj,94,61);saved_contain(obj);}
   for(const char *name:{"home_condition","home_city"})if(auto *obj=savedFind(name)){hidden(obj,true);saved_set_name(obj,!strcmp(name,"home_city")?"home_city_template":"home_condition_template");}
   // Fit the icon, 21px temperature line and two captions into the 65px
   // content height. The old y=36 backdrop painted over the temperature/unit.
   if(auto *header=savedFind("home_weather_header")){lv_obj_set_y(header,1);lv_obj_set_height(header,39);}
   if(auto *row=savedFind("home_temperature_row")){lv_obj_set_y(row,18);lv_obj_set_width(row,70);}
   if(auto *number=savedFind("home_temperature"))lv_obj_clear_flag(number,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
   auto *backdrop=panel(card,4,40,74,25,0x18251A,6);lv_obj_set_style_bg_opa(backdrop,110,0);lv_obj_clear_flag(backdrop,LV_OBJ_FLAG_CLICKABLE);
   homeCondition=label(card,"暂无",6,40,72,saved_gift_font_bold_11_0,0xFFFFFF);lv_label_set_long_mode(homeCondition,LV_LABEL_LONG_DOT);lv_obj_set_height(homeCondition,13);saved_set_name(homeCondition,"home_condition");
   homeCity=label(card,"",6,53,72,saved_gift_font_bold_10_0,0xEDF5D8);lv_label_set_long_mode(homeCity,LV_LABEL_LONG_DOT);lv_obj_set_height(homeCity,12);saved_set_name(homeCity,"home_city");
   lv_obj_move_foreground(homeCondition);lv_obj_move_foreground(homeCity);
  }
  // Connected state uses the supplied Wi-Fi glyph; offline the same glyph is
  // dimmed and two red strokes cross it out.
  if(auto *icon=savedFind("icon_80")){
   auto *holder=lv_obj_get_parent(icon);
   lv_img_set_src(icon,saved_gift_wifi_on);lv_obj_set_size(icon,13,10);lv_obj_set_pos(icon,0,0);
   lv_obj_set_style_img_recolor(icon,lv_color_hex(0x647956),0);lv_obj_add_style(icon,&UiStyles::iconTint,0);
   if(auto *caption=savedFind("text_79"))lv_obj_set_x(caption,15);
   static lv_point_t crossA[]={{1,1},{12,9}},crossB[]={{12,1},{1,9}};
   wifiCross=panel(holder,0,0,13,10,0,0);lv_obj_add_style(wifiCross,&UiStyles::bgTransparent,0);
   auto *a=lv_line_create(wifiCross);lv_line_set_points(a,crossA,2);lv_obj_set_style_line_width(a,2,0);lv_obj_set_style_line_color(a,lv_color_hex(0xc0392b),0);
   auto *b=lv_line_create(wifiCross);lv_line_set_points(b,crossB,2);lv_obj_set_style_line_width(b,2,0);lv_obj_set_style_line_color(b,lv_color_hex(0xc0392b),0);
   hidden(wifiCross,true);
  }
  }
 }
 if(index==0&&kDesignStatic)if(auto *city=savedFind("home_city")){
  auto *shadow=lv_label_create(lv_obj_get_parent(city));
  lv_label_set_text(shadow,lv_label_get_text(city));
  lv_obj_set_style_text_font(shadow,lv_obj_get_style_text_font(city,0),0);
  lv_obj_set_style_text_color(shadow,lv_color_hex(0x1c2a1e),0);
  lv_obj_set_style_text_opa(shadow,LV_OPA_70,0);
  lv_obj_set_pos(shadow,lv_obj_get_x(city)+1,lv_obj_get_y(city)+1);
  lv_obj_set_size(shadow,lv_obj_get_width(city),lv_obj_get_height(city));
  lv_obj_clear_flag(shadow,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(city);
  saved_set_name(shadow,"home_city_shadow");
 }
 if(index==1){savedBind("element_2",Ui::OpenPhone);savedBind("element_3",HomeCalendar);savedBind("element_120",Ui::OpenPhone);savedBind("weather_refresh",Ui::RefreshWeather);}
 if(index==2){for(const char *name:{"element_3","element_4","element_5"})savedBind(name,Ui::OpenPhone);savedBind("element_9",HolidayNew);savedBind("element_14",HolidaySpring);}
 if(index==3){savedBind("element_5",Ui::RefreshQuote);savedBind("element_6",OpenQuote);savedBind("element_15",Ui::RefreshPoem);savedBind("element_16",OpenPoem);
  // The author used to sit in an 84px box and clipped long dynasty/author pairs.
  // Give the whole line the card width, right-aligned; the small divider is no
  // longer needed once the text can grow to the left.
  hidden(savedFind("element_19"),true);
  if(auto *authorBox=savedFind("element_20")){lv_obj_set_x(authorBox,8);lv_obj_set_width(authorBox,260);}}
 if(index==4){savedBind("element_3",OpenMood);savedBind("mood_select",OpenMood);savedBind("wood_hit",Strike);savedBind("water_200",Water200);savedBind("water_500",Water500);savedBind("element_43",OpenWood);savedBind("element_49",OpenWater);for(int i=0;i<7;i++){char name[20];snprintf(name,sizeof(name),"element_%d",7+i*4);savedBind(name,280+i);}}
 if(index==4&&!kDesignStatic)for(int i=0;i<7;i++){char name[24];snprintf(name,sizeof(name),"element_%d",8+i*4);auto *ring=savedFind(name);if(ring){lv_obj_clean(ring);weekFaces[i]=lv_img_create(ring);lv_img_set_src(weekFaces[i],saved_gift_mood_calm_27);lv_obj_clear_flag(weekFaces[i],LV_OBJ_FLAG_CLICKABLE);weekPluses[i]=lv_img_create(ring);lv_img_set_src(weekPluses[i],saved_gift_decor_4_9);lv_obj_clear_flag(weekPluses[i],LV_OBJ_FLAG_CLICKABLE);}}
   if(index==6){savedBind("app_clock",OpenClock);savedBind("app_letter",Ui::OpenMessages);savedBind("app_weather",Ui::OpenWeather);savedBind("app_voice",Ui::OpenVoice);savedBind("app_calendar",HomeCalendar);savedBind("app_settings",Ui::OpenSettings);
  // The generated launcher still carries the old status cluster (Wi-Fi /
  // Bluetooth / battery). Delivery rules: no Bluetooth anywhere and the apps
  // page has no status bar, so hide the copied cluster and keep the artwork.
  for(const char *name:{"image_1_copy","image_2_copy","image_3_copy","label_3_copy","label_4_copy","label_6_copy"})hidden(savedFind(name),true);
    if(!kDesignStatic){
    {auto *bg=lv_img_create(screen);lv_img_set_src(bg,&gift_apps_bg);lv_obj_set_pos(bg,0,0);lv_obj_clear_flag(bg,LV_OBJ_FLAG_CLICKABLE);lv_obj_move_background(bg);}
    struct AppIcon{const char *card;const char *icon;const lv_img_dsc_t *src;};
    static const AppIcon apps[]={{"element_3","icon_4",&gift_app_clock},{"element_8","icon_9",&gift_app_table},{"element_13","icon_14",&gift_app_weather},{"element_18","icon_19",&gift_app_voice},{"element_23","icon_24",&gift_app_calendar},{"element_28","icon_29",&gift_app_settings}};
    for(auto &a:apps){
      if(auto *card=savedFind(a.card)){lv_obj_add_style(card,&UiStyles::bgTransparent,0);lv_obj_add_style(card,&UiStyles::flat,0);lv_obj_set_pos(card,18,-4);lv_obj_set_size(card,56,56);}
      if(auto *icon=savedFind(a.icon)){lv_obj_clear_flag(icon,LV_OBJ_FLAG_HIDDEN);lv_img_set_src(icon,a.src);lv_obj_set_pos(icon,0,0);lv_obj_set_size(icon,56,56);}
    }
    for(const char *n:{"text_2","text_7","text_12","text_17","text_22","text_27"})if(auto *l=savedFind(n)){lv_obj_set_style_text_font(l,saved_gift_yahei_b_12_75,0);lv_obj_set_style_text_color(l,lv_color_hex(0xfbf6e9),0);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_height(l,18);lv_obj_set_y(l,55);lv_obj_set_style_bg_color(l,lv_color_hex(0x0b1a12),0);lv_obj_set_style_bg_opa(l,96,0);lv_obj_set_style_pad_hor(l,3,0);lv_obj_set_style_pad_ver(l,1,0);lv_obj_set_style_radius(l,6,0);}
    if(auto *l=savedFind("text_2")){lv_obj_set_width(l,62);lv_obj_set_x(l,15);}
    if(auto *l=savedFind("text_7")){text(l,"课程表");lv_obj_set_width(l,50);lv_obj_set_x(l,21);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);}
    for(const char *n:{"text_12","text_17","text_22","text_27"})if(auto *l=savedFind(n)){lv_obj_set_width(l,38);lv_obj_set_x(l,27);}
    if(auto *v=savedFind("app_voice"))lv_obj_set_y(v,90);
    if(auto *c=savedFind("app_calendar"))lv_obj_set_y(c,90);
    if(auto *s=savedFind("app_settings"))lv_obj_set_y(s,90);
   }
   }
 // Runtime replacements rename/add widgets and may remove generated children.
 // Rebuild the name index on the first refresh instead of retaining stale entries.
 SavedDesign::invalidate();
}
void savedNumber(const char *name,const char *unit,int initialWidth,int unitX){
 auto *number=savedFind(name),*unitLabel=savedFind(unit);if(!number||!unitLabel)return;
 const auto *font=lv_obj_get_style_text_font(number,0);lv_point_t initial,current;
 int spacing=lv_obj_get_style_text_letter_space(number,0);
 lv_txt_get_size(&initial,"0",font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 lv_txt_get_size(&current,lv_label_get_text(number),font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
 int width=std::max(initialWidth,(int)current.x+2);if(lv_obj_get_style_width(number,0)!=width)lv_obj_set_width(number,width);
 if(lv_obj_get_parent(number)!=lv_obj_get_parent(unitLabel)){auto *wrapper=lv_obj_get_parent(number);if(lv_obj_get_style_width(wrapper,0)!=width)lv_obj_set_width(wrapper,width);unitLabel=lv_obj_get_parent(unitLabel);}
 int x=unitX+std::max(0,(int)current.x-initial.x);if(lv_obj_get_style_x(unitLabel,0)!=x)lv_obj_set_x(unitLabel,x);
}
int savedMeasuredWidth(lv_obj_t *obj){
 if(!obj)return 0;const char *value=lv_label_get_text(obj);if(!value)value="";
 const lv_font_t *font=lv_obj_get_style_text_font(obj,0);lv_point_t size;int spacing=lv_obj_get_style_text_letter_space(obj,0);
 lv_txt_get_size(&size,value,font,spacing,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);return (int)size.x;
}
void savedFitValue(const char *name,const char *wrapperName,const char *slotName,int maxWidth,int minimumWidth=1){
 auto *label=savedFind(name),*wrapper=savedFind(wrapperName),*slot=savedFind(slotName);if(!label)return;
 int width=std::max(minimumWidth,savedMeasuredWidth(label)+2);if(maxWidth>0)width=std::min(width,maxWidth);
 if(lv_obj_get_style_width(label,0)!=width)lv_obj_set_width(label,width);
 if(wrapper&&lv_obj_get_style_width(wrapper,0)!=width)lv_obj_set_width(wrapper,width);
 if(slot&&lv_obj_get_style_width(slot,0)<width)lv_obj_set_width(slot,width);
}
void savedFitMetric(const char *name,const char *valueWrapperName,const char *unitWrapperName,const char *slotName,int maxWidth){
 auto *value=savedFind(name),*wrapper=savedFind(valueWrapperName),*unit=savedFind(unitWrapperName),*slot=savedFind(slotName);if(!value||!wrapper)return;
 int valueWidth=std::max(2,savedMeasuredWidth(value)+2),unitWidth=unit?savedMeasuredWidth(lv_obj_get_child(unit,0))+1:0;
 int total=std::min(maxWidth,valueWidth+(unit?1+unitWidth:0));
 valueWidth=std::min(valueWidth,total-(unit?1+unitWidth:0));
 if(lv_obj_get_style_width(value,0)!=valueWidth)lv_obj_set_width(value,valueWidth);
 if(lv_obj_get_style_width(wrapper,0)!=total)lv_obj_set_width(wrapper,total);
 if(unit){if(lv_obj_get_style_x(unit,0)!=valueWidth+1)lv_obj_set_x(unit,valueWidth+1);if(lv_obj_get_style_width(unit,0)!=unitWidth)lv_obj_set_width(unit,unitWidth);auto *unitLabel=lv_obj_get_child(unit,0);if(unitLabel&&lv_obj_get_style_width(unitLabel,0)!=unitWidth)lv_obj_set_width(unitLabel,unitWidth);}
 if(slot&&lv_obj_get_style_width(slot,0)<total)lv_obj_set_width(slot,total);
}
int savedLineCount(const String &value){int lines=1;for(size_t i=0;i<value.length();++i)if(value[i]=='\n')++lines;return lines;}
String savedClampLines(const String &value,int limit){
 int line=1;for(size_t i=0;i<value.length();++i)if(value[i]=='\n'&&++line>limit)return value.substring(0,i)+"…";return value;
}
// Keep the first `limit` visual paragraphs before fitting: a four-line poem must
// still get the largest font that fits the two-line card rather than being
// rejected at every size and falling through to the smallest font.
String savedFirstLines(const String &value,int limit){
 int line=1;for(size_t i=0;i<value.length();++i)if(value[i]=='\n'&&++line>limit)return value.substring(0,i)+"…";return value;
}
int savedNaturalWidth(const String &value,const lv_font_t *font){lv_point_t size;lv_txt_get_size(&size,value.c_str(),font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);return (int)size.x;}
void savedApplyLabel(lv_obj_t *obj,const String &value,const lv_font_t *font,int width,int height,int lineSpace){
 if(!obj)return;
 if(lv_obj_get_width(obj)!=width)lv_obj_set_width(obj,width);
 if(lv_obj_get_height(obj)!=height)lv_obj_set_height(obj,height);
 if(lv_obj_get_style_text_font(obj,0)!=font)lv_obj_set_style_text_font(obj,font,0);
 if(lv_obj_get_style_text_line_space(obj,0)!=lineSpace)lv_obj_set_style_text_line_space(obj,lineSpace,0);
 if(lv_obj_has_flag(obj,LV_OBJ_FLAG_OVERFLOW_VISIBLE))lv_obj_clear_flag(obj,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
 if(String(lv_label_get_text(obj))!=value){lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);lv_label_set_text(obj,value.c_str());}
}
// Fit a multi-line dynamic text (quotes/poems) by shrinking the font until the
// pre-wrapped text fits the card. The API can return text much longer than the
// design placeholder; CLIP without this used to cut both sides of one line.
void savedWrapped(lv_obj_t *obj,const String &raw,int width,int height,const lv_font_t *const *fonts,int count,int lineSpace,int maxLines){
 if(!obj)return;String value=raw;value.replace("\r","");value.trim();if(value.isEmpty())value="—";
 const lv_font_t *chosen=fonts[count-1];String wrapped;
 for(int i=0;i<count;i++){
  String candidate=ChineseLayout::wrap(value,fonts[i],width);
  if(savedLineCount(candidate)>maxLines)continue;
  lv_point_t size;lv_txt_get_size(&size,candidate.c_str(),fonts[i],0,lineSpace,width,LV_TEXT_FLAG_NONE);
  if(size.y<=height){chosen=fonts[i];wrapped=candidate;break;}
 }
 if(wrapped.isEmpty())wrapped=savedClampLines(ChineseLayout::wrap(value,chosen,width),maxLines);
 savedApplyLabel(obj,wrapped,chosen,width,height,lineSpace);
}
// A single-line value such as an author name: shrink to fit, then ellipsize.
void savedShrinkLine(lv_obj_t *obj,const String &raw,int width,int height,const lv_font_t *const *fonts,int count){
 if(!obj)return;String value=raw;value.replace("\r","");value.trim();if(value.isEmpty())value="—";
 const lv_font_t *chosen=fonts[count-1];
 for(int i=0;i<count;i++)if(savedNaturalWidth(value,fonts[i])<=width){chosen=fonts[i];break;}
 if(lv_obj_get_width(obj)!=width)lv_obj_set_width(obj,width);
 if(lv_obj_get_height(obj)!=height)lv_obj_set_height(obj,height);
 if(lv_obj_get_style_text_font(obj,0)!=chosen)lv_obj_set_style_text_font(obj,chosen,0);
 if(String(lv_label_get_text(obj))!=value){lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);lv_label_set_text(obj,value.c_str());}
}
// Single-line value that may use more horizontal room when available.
void savedGrowLine(lv_obj_t *obj,lv_obj_t *wrapper,const String &raw,int maxWidth,const lv_font_t *const *fonts,int count){
 if(!obj)return;String value=raw;value.replace("\r","");value.trim();hidden(obj,value.isEmpty());if(value.isEmpty())return;
 const lv_font_t *chosen=fonts[count-1];
 for(int i=0;i<count;i++)if(savedNaturalWidth(value,fonts[i])<=maxWidth){chosen=fonts[i];break;}
 int width=std::min(maxWidth,std::max(18,savedNaturalWidth(value,chosen)+2));
 if(lv_obj_get_width(obj)!=width)lv_obj_set_width(obj,width);
 if(wrapper&&lv_obj_get_width(wrapper)!=width)lv_obj_set_width(wrapper,width);
 if(lv_obj_get_style_text_font(obj,0)!=chosen)lv_obj_set_style_text_font(obj,chosen,0);
 if(String(lv_label_get_text(obj))!=value){lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);lv_label_set_text(obj,value.c_str());}
}
// ---- weather refresh animation (saved design weather page) ----------------
void spinExec(void *var,int32_t value){lv_img_set_angle((lv_obj_t*)var,(int16_t)value);}
void spinDone(lv_anim_t *anim){auto *icon=(lv_obj_t*)lv_anim_get_user_data(anim);if(icon)lv_img_set_angle(icon,0);}
// One full turn of the big weather icon while a refresh is in flight. Rotation
// around the icon centre keeps the dirty area at the icon's own bounds.
void startWeatherSpin(){
 auto *icon=savedFind("icon_7");if(!icon)return;
 weatherSpinIcon=icon;stopAnim(icon);
 lv_img_set_pivot(icon,(lv_coord_t)(lv_obj_get_width(icon)/2),(lv_coord_t)(lv_obj_get_height(icon)/2));
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,icon);lv_anim_set_values(&anim,0,3600);
 lv_anim_set_time(&anim,700);lv_anim_set_path_cb(&anim,lv_anim_path_linear);lv_anim_set_exec_cb(&anim,spinExec);
 lv_anim_set_ready_cb(&anim,spinDone);lv_anim_set_user_data(&anim,icon);lv_anim_start(&anim);
}
void weatherFadeExec(void *var,int32_t value){lv_obj_set_style_img_opa((lv_obj_t*)var,(lv_opa_t)value,0);}
// Fade the fresh icon in when the data actually arrives; ends on LV_OPA_COVER,
// the value the designer page had before the animation existed.
void fadeWeatherIconIn(){
 auto *icon=savedFind("icon_7");if(!icon)return;
 stopAnim(icon);lv_obj_set_style_img_opa(icon,0,0);
 lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,icon);lv_anim_set_values(&anim,0,255);
 lv_anim_set_time(&anim,220);lv_anim_set_path_cb(&anim,Ui::easeInOut());lv_anim_set_exec_cb(&anim,weatherFadeExec);lv_anim_start(&anim);
}
static void savedFitImage(const char *name,const lv_img_dsc_t *src,int width,int height){
 auto *obj=savedFind(name);if(!obj||!src)return;
 if(lv_img_get_src(obj)!=(const void*)src)lv_img_set_src(obj,src);
 int zw=width*256/src->header.w,zh=height*256/src->header.h;int z=zw<zh?zw:zh;
 lv_img_set_zoom(obj,(uint16_t)z);
}
static const lv_img_dsc_t *moodFace27(const String &id){
 if(id=="happy")return saved_gift_mood_happy_27;
 if(id=="calm")return saved_gift_mood_calm_27;
 if(id=="neutral")return saved_gift_mood_neutral_27;
 if(id=="tired")return saved_gift_mood_tired_27;
 if(id=="anxious")return saved_gift_mood_anxious_27;
 if(id=="sad")return saved_gift_mood_sad_27;
 return nullptr;
}
static void fitDesignerIcon(lv_obj_t *obj,const lv_img_dsc_t *src,int width,int height){
 if(!obj||!src||!src->header.w||!src->header.h)return;
 int zw=width*256/src->header.w,zh=height*256/src->header.h;
 int zoom=zw<zh?zw:zh;
 lv_img_set_pivot(obj,0,0);lv_img_set_zoom(obj,(uint16_t)zoom);
}
static void setDesignerIcon(lv_obj_t *obj,const lv_img_dsc_t *src,int width,int height){
 if(!obj||!src)return;
 lv_obj_set_style_bg_img_src(obj,nullptr,0);
 lv_img_set_src(obj,src);
 fitDesignerIcon(obj,src,width,height);
 lv_obj_set_style_img_opa(obj,LV_OPA_COVER,0);
 lv_obj_set_style_img_recolor_opa(obj,LV_OPA_TRANSP,0);
 lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
}
static void savedSetDesignerIcon(const char *name,const lv_img_dsc_t *src,int width,int height){
 setDesignerIcon(savedFind(name),src,width,height);
}
void savedRefresh(){

 if(kDesignStatic&&savedIndex==0)return;
 int64_t now=ClockSource::now();bool valid=DeskLogic::timeValid(now);auto d=DeskLogic::beijingDate(now);int actual=dow(d);
 String today=GiftData::today();char value[80];
 if(savedIndex==0){
  snprintf(value,sizeof(value),"%02d",d.hour);savedText("clock_hour",valid?value:"--");snprintf(value,sizeof(value),"%02d",d.minute);savedText("clock_minute",valid?value:"--");
  snprintf(value,sizeof(value),"%02d/%02d 星期%s",d.month,d.day,week[actual]);savedText("clock_date",valid?value:"等待联网校时");
  const char *days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};savedText("clock_weekday",valid?days[actual]:"");
  static const lv_font_t *homeTempFonts[]={saved_gift_yahei_b_17_0,saved_gift_font_bold_14_0,&fd_sans_bold_12};
  auto *homeTemperature=savedFind("home_temperature");
  savedShrinkLine(homeTemperature,weather.valid?TextUtil::number(weather.temperature):"--",48,21,homeTempFonts,3);
  if(homeTemperature)if(auto *unit=savedFind("home_temperature_unit")){
   auto *unitText=savedFind("home_temperature_unit_text");
   const auto *numberFont=lv_obj_get_style_text_font(homeTemperature,0),*unitFont=unitText?lv_obj_get_style_text_font(unitText,0):nullptr;
   if(!numberFont||!unitFont)unitFont=saved_gift_font_bold_10_0;
   int measuredWidth=savedMeasuredWidth(homeTemperature);
   int x=measuredWidth+1,y=numberFont->line_height-numberFont->base_line-unitFont->line_height+unitFont->base_line;
   if(lv_obj_get_style_x(unit,0)!=x||lv_obj_get_style_y(unit,0)!=y)lv_obj_set_pos(unit,x,y);
  }
  static const lv_font_t *homeCityFonts[]={saved_gift_font_bold_10_0,saved_gift_font_bold_9_0,saved_gift_font_bold_8_0};
  savedShrinkLine(homeCity,settings.city.isEmpty()?"未设置城市":settings.city,72,12,homeCityFonts,3);
  static const lv_font_t *homeConditionFonts[]={saved_gift_font_bold_11_0,saved_gift_font_bold_10_0,saved_gift_font_bold_9_0};
  savedShrinkLine(homeCondition,weather.valid?(weather.condition.isEmpty()?String(weatherName(weather.code)):weather.condition):"暂无",72,13,homeConditionFonts,3);
  textColor(homeCondition,0xFFFFFF);textColor(homeCity,0xEDF5D8);
  savedText("calendar_title",valid?String(d.month)+"月"+d.day+"日":"未校时");
  int offset=((DeskLogic::civilDays(d.year,d.month,1)+3)%7+7)%7,daysInMonth=DeskLogic::daysInMonth(d.year,d.month),rows=(offset+daysInMonth+6)/7,step=rows==6?7:8;
  for(int day=1;day<=31;day++){char name[24];snprintf(name,sizeof(name),"calendar_%d",day);auto *obj=savedFind(name);hidden(obj,!valid||day>daysInMonth);if(obj&&valid&&day<=daysInMonth){int slot=offset+day-1,x=(slot%7*96+3)/7,y=slot/7*step;if(lv_obj_get_style_x(obj,0)!=x||lv_obj_get_style_y(obj,0)!=y)lv_obj_set_pos(obj,x,y);textColor(obj,day==d.day?0xffffff:0xe9eddf);if(day==d.day){auto *mark=savedFind("calendar_today");if(mark&&(lv_obj_get_style_x(mark,0)!=x+2||lv_obj_get_style_y(mark,0)!=y-1))lv_obj_set_pos(mark,x+2,y-1);}}}
  hidden(savedFind("calendar_today"),!valid);
  for(int i=0;i<3;i++){auto other=DeskLogic::beijingDate(now+(i-1)*86400);char name[24];snprintf(name,sizeof(name),"week_day_%d",i);savedText(name,valid?week[dow(other)]:"-");snprintf(name,sizeof(name),"week_number_%d",i);savedText(name,valid?String(other.day):"--");}
  auto power=PowerStatus::get();bool online=WiFi.status()==WL_CONNECTED;
  savedText("text_79","Wi-Fi");
  if(auto *icon=savedFind("icon_80"))lv_obj_set_style_img_opa(icon,online?255:70,0);
  hidden(wifiCross,online);
  if(auto *icon=savedFind("icon_85")){
   if(power.chargingKnown&&power.charging){
    lv_img_set_src(icon,saved_gift_charge);lv_obj_set_size(icon,16,11);lv_obj_set_pos(icon,0,4);
    // Refresh runs at 1 Hz; never call lv_obj_add_style() here. Adding the same
    // style every tick grows the object's style list without bound and froze the
    // main loop (TG1WDT) about a minute after boot. A local style property has
    // the same visual result and is idempotent.
    lv_obj_set_style_img_recolor(icon,lv_color_hex(0x526642),0);lv_obj_set_style_img_recolor_opa(icon,LV_OPA_COVER,0);
   }else{
    lv_img_set_src(icon,saved_gift_battery);lv_obj_set_size(icon,15,12);lv_obj_set_pos(icon,0,3);
    lv_obj_set_style_img_recolor_opa(icon,LV_OPA_TRANSP,0);
   }
  }
  String status;
  if(power.installed)status=power.voltageValid?"约 "+String(power.percent)+"%":"电量未知";
  hidden(savedFind("power_status"),status.isEmpty());
  savedText("power_status",status);
 }
 if(savedIndex==3){
  char dateBuf[10];snprintf(dateBuf,sizeof(dateBuf),"%02d/%02d",d.month,d.day);savedText("reading_date",valid?dateBuf:"--/--");
  String quoteText=readingValue("quote","Text");
  savedText("quote_text",ChineseLayout::wrap(quoteText,saved_gift_yahei_b_16_1,283));
  if(auto *qt=savedFind("quote_text"))lv_obj_set_style_text_align(qt,LV_TEXT_ALIGN_CENTER,0);
  if(auto *qa=savedFind("quote_author"))lv_obj_set_style_text_align(qa,LV_TEXT_ALIGN_CENTER,0);
  String quoteAuthor=readingValue("quote","Author");if(quoteAuthor.isEmpty())quoteAuthor="佚名";savedText("quote_author",quoteAuthor);
  String verse=readingValue("poem","Text");
  verse.replace("，\n","，");verse.replace("。\n","。");verse.replace("，","，\n");verse.replace("。","。\n");
  while(!verse.isEmpty()&&verse[verse.length()-1]=='\n')verse=verse.substring(0,verse.length()-1);
  savedText("poem_text",ChineseLayout::wrap(savedFirstLines(verse,2),saved_gift_yahei_b_16_1,276));
  String dynasty=readingValue("poem","Dynasty");
  if(auto *pa=savedFind("poem_author")){lv_obj_set_x(pa,120);lv_obj_set_width(pa,182);}
  String poemAuthor=readingValue("poem","Author");if(poemAuthor.isEmpty())poemAuthor="佚名";savedText("poem_author",(dynasty.isEmpty()?String(""):dynasty+"代 · ")+poemAuthor);
  return;
 }
 if(savedIndex==4){
  auto record=GiftData::record(today);int water=waterTotal(record),goal=GiftData::get("daily")["waterGoal"]|1000;
  int knocks=(int)(record["knocks"]|0);
  if(auto *count=savedFind("wood_count")){lv_obj_set_x(count,knocks>=10?2:8);lv_obj_set_width(count,knocks>=10?22:12);}
  if(auto *unit=savedFind("text_37_1"))lv_obj_set_x(unit,knocks>=10?25:20);
  savedText("wood_count",String(knocks));savedText("text_37_1"," 次");
  savedText("water_value",String(water));
  savedText("water_goal",String(std::min(999,water*100/goal))+"% · "+String(goal)+" ml");
  int monday=(actual+6)%7;String signature;
  for(int i=0;i<7;i++){auto date=DeskLogic::beijingDate(now+(i-monday)*86400);signature+=String(GiftData::record(dateKey(date))["mood"]|"")+",";}
  static String lastMoodSignature;
  if(signature!=lastMoodSignature){
   lastMoodSignature=signature;
   for(int i=0;i<7;i++){
    auto date=DeskLogic::beijingDate(now+(i-monday)*86400);String mood=GiftData::record(dateKey(date))["mood"]|"";
    char ringName[24];snprintf(ringName,sizeof(ringName),"element_%d",8+i*4);auto *ring=savedFind(ringName);if(!ring)continue;
    lv_obj_clean(ring);
    if(mood.isEmpty()){auto *plus=lv_img_create(ring);lv_img_set_src(plus,saved_gift_decor_4_9);lv_obj_set_pos(plus,2,2);lv_obj_clear_flag(plus,LV_OBJ_FLAG_CLICKABLE);}
    else{const lv_img_dsc_t *face=moodFace27(mood);if(!face)face=saved_gift_mood_calm_27;auto *img=lv_img_create(ring);lv_img_set_src(img,face);lv_obj_set_size(img,27,27);lv_obj_clear_flag(img,LV_OBJ_FLAG_CLICKABLE);}
    lv_obj_set_style_border_width(ring,mood.isEmpty()?2:0,0);
    lv_obj_set_style_border_color(ring,lv_color_hex(0xffdf6c),0);
   }
   if(auto *underline=savedFind("today_underline")){
    int todayIndex=actual==0?6:(actual-1);
    char dayName[24];snprintf(dayName,sizeof(dayName),"element_%d",7+todayIndex*4);
    if(auto *dayButton=savedFind(dayName)){lv_obj_set_parent(underline,dayButton);lv_obj_set_pos(underline,7,43);lv_obj_set_width(underline,23);}
    else lv_obj_add_flag(underline,LV_OBJ_FLAG_HIDDEN);
   }
  }
  return;
 }
 if(savedIndex==6){
  auto power=PowerStatus::get();bool online=WiFi.status()==WL_CONNECTED;
  savedSetDesignerIcon("image_1_copy",online?saved_gift_status_wifi:saved_gift_status_offline,17,17);
  savedSetDesignerIcon("image_2_copy",saved_gift_status_bt,17,17);
  savedSetDesignerIcon("image_3_copy",power.external?saved_gift_status_battery:(power.low?saved_gift_status_battery_low:saved_gift_status_battery_fill),29,18);
  savedText("label_3_copy",online?"Online":"离线");
  savedText("label_4_copy","BT ON");
  char percentText[16];snprintf(percentText,sizeof(percentText),"%d %%",(power.installed&&power.voltageValid)?power.percent:-1);
  savedText("label_6_copy",power.voltageValid?String(percentText):String("—"));
  return;
 }
 if(savedIndex==1){
  static bool wasPending=false;
  if(weatherPending!=wasPending){wasPending=weatherPending;}
  savedText("weather_city",settings.city.isEmpty()?"未设置城市":settings.city);
  char dateBuf[40];snprintf(dateBuf,sizeof(dateBuf),"%d月%d日 星期%s",d.month,d.day,week[actual]);
  savedText("weather_date",valid?dateBuf:"等待联网校时");
  savedText("weather_updated",weatherPending?"更新中":weather.valid?("上次更新 "+TextUtil::formatDate(weather.fetchedAt,"%H:%M")):"上次更新 —");
  savedText("condition",weather.valid?(weather.condition.isEmpty()?String(weatherName(weather.code)):weather.condition):"多云");
  savedText("temperature",weather.valid?TextUtil::number(weather.temperature):"--");
  savedSource("icon_7",savedWeatherImage(weather.valid?weather.code:-1));
  savedText("text_27",weather.valid&&!weather.sunrise.isEmpty()?weather.sunrise:"--:--");
  savedText("text_36",weather.valid&&!weather.sunset.isEmpty()?weather.sunset:"--:--");
  savedText("text_45",weather.valid?TextUtil::number(weather.pressure):"—");
  savedText("text_54",weather.valid?TextUtil::number(weather.uv):"—");
  savedText("text_63",weather.valid?TextUtil::number(weather.visibility):"—");
  savedText("text_73",weather.valid?TextUtil::number(weather.aqi)+(weather.aqiCategory.isEmpty()?"":" "+weather.aqiCategory):"—");
  savedText("feels_like","体感温度 "+TextUtil::number(weather.feels,"°"));
  savedText("weather_range",TextUtil::number(weather.high,"°")+" / "+TextUtil::number(weather.low,"°"));
  savedText("weather_extra","湿度 "+TextUtil::number(weather.humidity,"%")+"      风况"+(weather.windText.isEmpty()?TextUtil::number(weather.wind," km/h"):weather.windText));
  for(int i=0;i<7;i++){
   auto f=weather.forecast[i];int y=0,m=0,day=0;bool dated=sscanf(f.date.c_str(),"%d-%d-%d",&y,&m,&day)==3&&DeskLogic::validDate(y,m,day);
   auto fd=dated?DeskLogic::beijingDate(DeskLogic::beijingEpoch(y,m,day)):DeskLogic::beijingDate(now+i*86400);
   static const char *weekNames[]={"text_77","text_83","text_89","text_95","text_101","text_107","text_113"};
   static const char *rangeNames[]={"text_81","text_87","text_93","text_99","text_105","text_111","text_117"};
   static const char *iconNames[]={"icon_78","icon_84","icon_90","icon_96","icon_102","icon_108","icon_114"};
   savedText(weekNames[i],valid?String("周")+week[dow(fd)]:"--");
   savedText(rangeNames[i],TextUtil::number(f.high,"°")+" / "+TextUtil::number(f.low,"°"));
   if(weather.valid)savedSource(iconNames[i],savedWeatherImage(f.code,true));
  }
  return;
 }
 if(savedIndex==2){
  auto holiday=GiftData::get("holiday");int target=holiday["weekday"]|5,remaining=(target-actual+7)%7;String holidayLabel=holiday["label"]|"放假";
  savedText("holiday_question",valid?String("今天是周")+week[target]+"么？":"今天几号？");
  String answer=!valid?"等待联网校时":remaining==0?String("是的，今天是")+holidayLabel:String("不是，还有")+String(remaining)+"天";
  savedText("holiday_answer",answer);
  savedText("holiday_weekday",english[target]);
  savedText("holiday_day",holidayLabel);
  int64_t day=DeskLogic::civilDays(d.year,d.month,d.day);int nextYear=d.month==1&&d.day==1?d.year:d.year+1;
  savedText("newyear_days",valid?String((long)(DeskLogic::civilDays(nextYear,1,1)-day)):"--");
  int year=d.year,spring=-1;if(year>=1900&&year<=2100){int date=GiftSpringDates[year-1900];spring=DeskLogic::civilDays(year,date/100,date%100)-day;if(spring<0&&year<2100){date=GiftSpringDates[++year-1900];spring=DeskLogic::civilDays(year,date/100,date%100)-day;}}
  savedText("spring_days",valid?String((long)(spring>=0?spring:(DeskLogic::civilDays(d.year+1,2,1)-day))):"--");
  return;
 }
 if(savedIndex==1){
  // Weather refresh feedback: spin once when a request starts, fade the new icon
  // in when the data lands. Rides on the existing pending flag, so static pages
  // never animate.
  if(weatherPending!=weatherWasPending){if(weatherPending)startWeatherSpin();else fadeWeatherIconIn();weatherWasPending=weatherPending;}
  savedText("weather_city",settings.city.isEmpty()?"未设置城市":settings.city);snprintf(value,sizeof(value),"%d月%d日 星期%s",d.month,d.day,week[actual]);savedText("weather_date",valid?value:"等待联网校时");
  savedText("temperature",weather.valid?TextUtil::number(weather.temperature):"--");savedText("condition",weather.valid?(weather.condition.isEmpty()?String(weatherName(weather.code)):weather.condition):"暂无天气");
  savedSource("icon_7",savedWeatherImage(weather.valid?weather.code:-1));
  savedText("feels_like","体感温度 "+TextUtil::number(weather.feels,"°"));savedText("weather_range",TextUtil::number(weather.high,"°")+" / "+TextUtil::number(weather.low,"°"));
  savedText("weather_extra","湿度 "+TextUtil::number(weather.humidity,"%")+" · "+(weather.windText.isEmpty()?TextUtil::number(weather.wind," km/h"):weather.windText));
  savedText("weather_updated",weatherPending?"更新中":weather.valid?"上次更新 "+TextUtil::formatDate(weather.fetchedAt,"%H:%M"):"上次更新 —");
  savedText("text_27",weather.sunrise);savedText("text_36",weather.sunset);savedText("text_45",TextUtil::number(weather.pressure));savedText("text_54",TextUtil::number(weather.uv));savedText("text_63",TextUtil::number(weather.visibility));savedText("text_73",TextUtil::number(weather.aqi)+(!weather.aqiCategory.isEmpty()?" "+weather.aqiCategory:String("")));
  // The exported design uses compact placeholder widths. Fit live values to
  // the same slots so digits and units remain visible without changing the
  // colors, fonts, or the surrounding composition.
  savedFitValue("temperature","element_13","element_12",52,20);
  savedFitValue("feels_like","element_16","element_12",66,42);
  savedFitValue("weather_range","element_17","element_12",66,41);
  savedFitValue("weather_extra","element_18","element_9",138,80);
  savedFitValue("text_27","element_27","element_25",54,31);
  savedFitValue("text_36","element_36","element_34",54,31);
  savedFitMetric("text_45","element_45","element_46","element_42",54);
  savedFitValue("text_54","element_54","element_52",54,18);
  savedFitMetric("text_63","element_63","element_64","element_58",54);
  savedFitValue("text_73","element_73","element_71",54,18);
  for(int i=0;i<7;i++){auto f=weather.forecast[i];int year=0,month=0,day=0;bool dated=sscanf(f.date.c_str(),"%d-%d-%d",&year,&month,&day)==3&&DeskLogic::validDate(year,month,day);auto date=dated?DeskLogic::beijingDate(DeskLogic::beijingEpoch(year,month,day)):DeskLogic::beijingDate(now+i*86400);char name[24];snprintf(name,sizeof(name),"text_%d",77+i*6);savedText(name,valid?"周"+String(week[dow(date)]):"--");snprintf(name,sizeof(name),"text_%d",81+i*6);savedText(name,TextUtil::number(f.high,"°")+" / "+TextUtil::number(f.low,"°"));snprintf(name,sizeof(name),"icon_%d",78+i*6);savedSource(name,savedWeatherImage(f.code,true));}
  savedText("weather_status",weatherPending?"天气更新中…":weather.valid?"预报来源 · UApi":weather.error.isEmpty()?"预报来源 · 等待联网更新":weather.error);
 }
 if(savedIndex==2){
  auto holiday=GiftData::get("holiday");int target=holiday["weekday"]|5,remaining=(target-actual+7)%7;String holidayLabel=holiday["label"]|"放假";
  static const lv_font_t *questionFonts[]={saved_gift_yahei_r_16_0,saved_gift_yahei_r_15_4,&fd_sans_bold_12};
  auto *question=savedFind("holiday_question");if(question)lv_obj_set_y(question,1);
  savedShrinkLine(question,String("今天是周")+week[target]+"么？",130,22,questionFonts,3);
  String answer=!valid?"等待联网校时":remaining==0?String("是的，今天是")+holidayLabel:String("不是，还有")+String(remaining)+"天";
  static const lv_font_t *answerFonts[]={saved_gift_yahei_r_21_0,saved_gift_yahei_r_19_0,saved_gift_yahei_r_18_4,saved_gift_yahei_r_17_0};
  savedShrinkLine(savedFind("holiday_answer"),answer,170,24,answerFonts,4);
  static const lv_font_t *dayFonts[]={saved_gift_yahei_bi_25_9,saved_gift_yahei_b_22_0,saved_gift_yahei_r_18_4,saved_gift_yahei_r_17_0};
  savedShrinkLine(savedFind("holiday_day"),holidayLabel,72,32,dayFonts,4);
  static const lv_font_t *weekFonts[]={saved_gift_segoe_ri_11_48,saved_gift_segoe_r_9_0,saved_gift_segoe_r_8_0};
  savedShrinkLine(savedFind("holiday_weekday"),english[target],59,13,weekFonts,3);
  int64_t day=DeskLogic::civilDays(d.year,d.month,d.day);int nextYear=d.month==1&&d.day==1?d.year:d.year+1;savedText("newyear_days",valid?String((long)(DeskLogic::civilDays(nextYear,1,1)-day)):"--");
  int year=d.year,spring=-1;if(year>=1900&&year<=2100){int date=GiftSpringDates[year-1900];spring=DeskLogic::civilDays(year,date/100,date%100)-day;if(spring<0&&year<2100){date=GiftSpringDates[++year-1900];spring=DeskLogic::civilDays(year,date/100,date%100)-day;}}savedText("spring_days",valid&&spring>=0?String(spring):"--");
 }
 if(savedIndex==3){
  snprintf(value,sizeof(value),"%02d/%02d",d.month,d.day);savedText("reading_date",valid?value:"--/--");
  if(auto *date=savedFind("reading_date")){lv_obj_set_y(date,0);lv_obj_set_height(date,24);}
  static const lv_font_t *quoteFonts[]={saved_gift_yahei_b_17_0,saved_gift_yahei_r_16_0,saved_gift_yahei_r_15_4,&fd_sans_bold_12,saved_gift_font_bold_11_0,saved_gift_font_bold_10_0};
  savedWrapped(savedFind("quote_text"),readingValue("quote","Text"),278,46,quoteFonts,6,3,2);
  static const lv_font_t *quoteAuthorFonts[]={saved_gift_yahei_b_11_0,saved_gift_font_bold_10_0,saved_gift_font_bold_9_0};
  auto *quoteAuthor=savedFind("quote_author");
  if(quoteAuthor)lv_obj_set_style_text_align(quoteAuthor,LV_TEXT_ALIGN_LEFT,0);
  savedGrowLine(quoteAuthor,savedFind("element_12"),readingValue("quote","Author"),135,quoteAuthorFonts,3);
  String verse=readingValue("poem","Text");verse.replace("，\n","，");verse.replace("。\n","。");verse.replace("，","，\n");verse.replace("。","。\n");while(!verse.isEmpty()&&verse[verse.length()-1]=='\n')verse=verse.substring(0,verse.length()-1);
  static const lv_font_t *poemFonts[]={saved_gift_kai_r_18_0,saved_gift_yahei_r_16_0,saved_gift_yahei_r_15_4,saved_gift_kai_r_12_0,saved_gift_kai_r_11_0,saved_gift_kai_r_10_0,saved_gift_kai_r_9_0};
  savedWrapped(savedFind("poem_text"),savedFirstLines(verse,2),146,44,poemFonts,7,2,2);
  String dynasty=readingValue("poem","Dynasty");String author=(dynasty.isEmpty()?String(""):dynasty+"代 · ")+readingValue("poem","Author");
  static const lv_font_t *poemAuthorFonts[]={saved_gift_kai_r_12_0,saved_gift_kai_r_11_0,saved_gift_kai_r_10_0,saved_gift_kai_r_9_0};
  auto *poemAuthor=savedFind("poem_author");
  if(poemAuthor)lv_obj_set_style_text_align(poemAuthor,LV_TEXT_ALIGN_RIGHT,0);
  savedShrinkLine(poemAuthor,author,254,13,poemAuthorFonts,4);
 }
 if(savedIndex==4){
  auto record=GiftData::record(today);int water=waterTotal(record),goal=GiftData::get("daily")["waterGoal"]|1000;savedText("wood_count",String(record["knocks"]|0));savedText("water_value",String(water));savedText("water_goal",String(std::min(999,water*100/std::max(1,goal)))+"% · "+goal+" ml");savedNumber("wood_count","text_37_1",11,19);savedNumber("water_value","water_unit",14,11);
  String mood=record["mood"]|"";savedSource("today_mood",savedMoodImage(mood));hidden(savedFind("today_mood"),mood.isEmpty());hidden(savedFind("mood_plus"),!mood.isEmpty());
  int monday=(actual+6)%7;for(int i=0;i<7;i++){auto date=DeskLogic::beijingDate(now+(i-monday)*86400);String face=GiftData::record(dateKey(date))["mood"]|"";char name[24];snprintf(name,sizeof(name),"element_%d",7+i*4);auto *day=savedFind(name);if(day){if(!valid||i>monday)lv_obj_add_state(day,LV_STATE_DISABLED);else lv_obj_clear_state(day,LV_STATE_DISABLED);}source(weekFaces[i],savedMoodImage(face,true));hidden(weekFaces[i],face.isEmpty());hidden(weekPluses[i],!face.isEmpty()||!valid||i>monday);snprintf(name,sizeof(name),"element_%d",8+i*4);auto *ring=savedFind(name);if(ring){int borderWidth=face.isEmpty()?2:0;if(lv_obj_get_style_border_width(ring,0)!=borderWidth)lv_obj_set_style_border_width(ring,borderWidth,0);if(lv_obj_get_style_border_color(ring,0).full!=lv_color_hex(0xffdf6c).full)lv_obj_set_style_border_color(ring,lv_color_hex(0xffdf6c),0);}if(i==monday){snprintf(name,sizeof(name),"element_%d",10+i*4);auto *holder=savedFind(name),*mark=savedFind("today_underline");if(holder&&mark&&lv_obj_get_parent(mark)!=holder)lv_obj_set_parent(mark,holder);}}
 }
}
void event(lv_event_t *e){
 if(Touch::swiped())return;int action=(uintptr_t)lv_event_get_user_data(e);lastInputAt=millis();
 if(action>=1000&&action<1007){section="";GiftPages::openPage(action-1000);return;}
  if(action<100){if(action==Ui::OpenMessages){section="";GiftPages::openPage(5);}else{
   switch(action){
     case Ui::OpenWeather:section="";GiftPages::openPage(1);break;
     case Ui::OpenVoice:section="";GiftPages::openApp((int)App::Voice);break;   // openApp performs XiaozhiVoice::launch()
     case Ui::OpenCalendar:
      if(!GiftData::today().isEmpty()){auto d=DeskLogic::beijingDate(ClockSource::now());calendarYear=d.year;calendarMonth=d.month;selectedDate={(uint16_t)d.year,(int8_t)d.month,(int8_t)d.day};}
      section="";GiftPages::openApp((int)App::Calendar);break;
     case Ui::OpenSettings:section="";GiftPages::openApp((int)App::Settings);break;
     case Ui::OpenPhone:section="";GiftPages::openApp((int)App::Phone);break;
     case OpenAlarmApp:section="";GiftPages::openApp((int)App::Alarm);break;
     case OpenPomodoroApp:section="";GiftPages::openApp((int)App::Pomodoro);break;
     case OpenTetrisApp:section="";GiftPages::openApp((int)App::Tetris);break;
     case OpenSnakeApp:section="";GiftPages::openApp((int)App::Snake);break;
     case OpenJumpApp:section="";GiftPages::openApp((int)App::Jump);break;
     case OpenSheepApp:section="";GiftPages::openApp((int)App::Sheep);break;
     case OpenMinerApp:section="";GiftPages::openApp((int)App::Miner);break;
     case OpenPlaneApp:section="";GiftPages::openApp((int)App::Plane);break;
     case OpenBreakoutApp:section="";GiftPages::openApp((int)App::Breakout);break;
     case OpenMinesApp:section="";GiftPages::openApp((int)App::Mines);break;
     case OpenPinApp:section="";GiftPages::openApp((int)App::Pin);break;
     case OpenMemoryApp:section="";GiftPages::openApp((int)App::Memory);break;

     case OpenMusicApp:section="";GiftPages::openApp((int)App::Music);break;
     case OpenRadioApp:section="";GiftPages::openApp((int)App::Radio);break;
     case OpenTimerApp:section="";GiftPages::openApp((int)App::Timer);break;
     case OpenDaysApp:section="";GiftPages::openApp((int)App::Days);break;
     case OpenGamesApp:section="";GiftPages::openApp((int)App::Games);break;
     case GamesPrevious:if(app==App::Games&&gamesSheet>0){--gamesSheet;rerender();}break;
     case GamesNext:if(app==App::Games&&gamesSheet<GamesLastSheet){++gamesSheet;rerender();}break;
     case Ui::Back:if(!back()){app=App::None;page=6;section="";rerender();}break;  // unreachable legacy: desk.cpp owns Back
     default:Ui::dispatchAction(e);break;}}
  return;
 }
 if(action>=200&&action<206){String error;auto record=GiftData::record(editorDate);if(GiftData::saveMoodAt(editorDate,moodIds[action-200],record["note"]|"",error)){section="";rerender();}else text(moodError,error);return;}
 if(action>=280&&action<287){int64_t now=ClockSource::now();auto d=DeskLogic::beijingDate(now);int monday=(dow(d)+6)%7;editorDate=dateKey(DeskLogic::beijingDate(now+(action-280-monday)*86400));if(editorDate>GiftData::today())return;auto record=GiftData::record(editorDate);moodDraft=record["mood"]|"";section="mood";rerender();return;}
 switch(action){
  case HomeCalendar:case HomeCountdown:{if(!GiftData::today().isEmpty()){auto d=DeskLogic::beijingDate(ClockSource::now());calendarYear=d.year;calendarMonth=d.month;selectedDate={(uint16_t)d.year,(int8_t)d.month,(int8_t)d.day};}calendarConfirm=false;section="";GiftPages::openApp((int)(action==HomeCalendar?App::Calendar:App::Countdown));break;}
  case OpenMood:case MessageEdit:{editorDate=action==MessageEdit?openedDate:GiftData::today();auto d=GiftData::record(editorDate);moodDraft=d["mood"]|"";page=4;app=App::None;section="mood";rerender();break;}
  case CancelMood:section="";rerender();break;
  case Strike:GiftData::strike();if(GiftData::get("daily")["sound"]|true)AudioBoard::knock();if(woodFeedback&&!GiftData::today().isEmpty()){woodFeedbackUntil=millis()+650;showWoodFeedback();}knockWood();lastRefresh=0;break;
  case ResetKnocks:GiftData::resetKnocks();lastRefresh=0;break;
  case Water200:case Water500:GiftData::addWater(action==Water200?200:500);lastRefresh=0;if(section=="water")rerender();break;case Water100:GiftData::addWater(100);lastRefresh=0;break;case UndoWater:GiftData::undoWater();lastRefresh=0;if(section=="water")rerender();break;
  case OpenWater:section="water";rerender();break;case OpenWood:section="wood";rerender();break;case UndoStrike:GiftData::undoStrike();lastRefresh=0;break;
  case ToggleWoodSound:{JsonDocument patch;patch["daily"]["sound"]=!(GiftData::get("daily")["sound"]|true);String error;if(GiftData::patch(patch.as<JsonVariantConst>(),error))rerender();else{notifyUser(error,true);rerender();}break;}
  case OpenQuote:section="reading-quote";rerender();break;case OpenPoem:section="reading-poem";rerender();break;
  case HolidayNew:case HolidaySpring:{auto d=DeskLogic::beijingDate(ClockSource::now());int y=d.year,m=1,day=1;if(action==HolidayNew){if(d.month!=1||d.day!=1)++y;}else if(y>=1900&&y<=2100){int date=GiftSpringDates[y-1900];m=date/100;day=date%100;if(DeskLogic::civilDays(y,m,day)<DeskLogic::civilDays(d.year,d.month,d.day)&&y<2100){date=GiftSpringDates[++y-1900];m=date/100;day=date%100;}}section="";GiftPages::openApp((int)App::Calendar);
   // openApp() starts Calendar with a clean selection; seed the holiday date after
   // the entry call so the calendar still opens with that day highlighted.
   selectedDate={(uint16_t)y,(int8_t)m,(int8_t)day};calendarYear=y;calendarMonth=m;break;}
  case MessageMood:case MessageLetter:case MessageKeepsake:detailKind=action==MessageMood?"mood":action==MessageLetter?"letter":"keepsake";openedDate=GiftData::today();section="detail";rerender();break;
  case MessageClose:section="";rerender();break;
  case OpenClock:clockSheet=0;section="";GiftPages::openApp((int)App::Clock);break;
  case ClockLandscape:case ClockMatrix:case ClockFlip:case ClockDashboard:case ClockMinimal:case ClockCompact:case ClockBarbar:{JsonDocument patch;patch["clockMode"]["preset"]=action==ClockLandscape?"landscape":action==ClockMatrix?"matrix":action==ClockFlip?"flip":action==ClockDashboard?"dashboard":action==ClockMinimal?"minimal":action==ClockCompact?"compact":"barbar";String error;if(GiftData::patch(patch.as<JsonVariantConst>(),error)){section="clock-face";rerender();}else{notifyUser(error,true);rerender();}break;}
  case ClockSettings:section="clock-settings";rerender();break;
  case ClockPicker:section="";rerender();break;
  case ClockPrevious:if(app==App::Clock&&section.isEmpty()&&clockSheet>0){--clockSheet;rerender();}break;
  case ClockNext:if(app==App::Clock&&section.isEmpty()&&clockSheet<1){++clockSheet;rerender();}break;
  default:break;
 }
}
}
void reset(){section="";holding=false;activePageModule=-1;clockSheet=0;}
bool inputActive(){return holding||(app==App::Clock&&DashboardClockSettings::inputActive())||(app==App::Breakout&&BreakoutPage::inputActive())||(app==App::Mines&&MinesPage::inputActive())||(app==App::Pin&&PinPage::inputActive())||(app==App::Memory&&MemoryPage::inputActive())||(app==App::Miner&&MinerPage::inputActive())||(app==App::Plane&&PlanePage::inputActive())||(app==App::Sheep&&SheepPage::inputActive())||(app==App::Jump&&JumpPage::inputActive())||(app==App::Timer&&TimerPage::inputActive())||(app==App::Days&&DaysPage::inputActive())||(app==App::Music&&MusicPage::inputActive())||(app==App::Radio&&RadioPage::inputActive());}
bool swipeApps(int dx){
 if(app==App::Games){
  if(dx<0&&gamesSheet<GamesLastSheet){++gamesSheet;rerender();return true;}
  if(dx>0&&gamesSheet>0){--gamesSheet;rerender();return true;}
  return false;
 }
 if(app!=App::None||page!=6)return false;
 if(dx<0&&appsSheet<AppsLastSheet){++appsSheet;rerender();return true;}
 if(dx>0&&appsSheet>0){--appsSheet;rerender();return true;}
 return false;
}
unsigned appsPageIndex(){return appsSheet;}
void setAppsPage(unsigned value){appsSheet=value>AppsLastSheet?AppsLastSheet:value;}
// The cached-home path never calls render(): stop the current view's animations
// and forget which page module owns the layer.
void leaveView(){activePageModule=-1;cancelPageAnimations();}
// Page-level back: hand-written page details and the legacy `section` views only.
// App-level return (per-App origin table) lives in desk.cpp's back(); desk.cpp is
// not compiled for the host, so the offscreen runner keeps the old Voice/Clock
// branches behind GIFT_HOST_TEST to preserve its historical assertions.
bool back(){
 if(app==App::Clock&&section=="clock-settings"&&DashboardClockSettings::back())return true;
 if((app==App::Alarm||app==App::Pomodoro)&&FocusApps::back())return true;
 if(app==App::Timer&&TimerPage::back())return true;
 if(app==App::Days&&DaysPage::back())return true;
 if(app==App::Tetris&&TetrisPage::back())return true;
 if(app==App::Snake&&SnakePage::back())return true;
 if(app==App::Jump&&JumpPage::back())return true;
 if(app==App::Sheep&&SheepPage::back())return true;
 if(app==App::Miner&&MinerPage::back())return true;
 if(app==App::Plane&&PlanePage::back())return true;
 if(app==App::Breakout&&BreakoutPage::back())return true;
 if(app==App::Mines&&MinesPage::back())return true;
 if(app==App::Pin&&PinPage::back())return true;
 if(app==App::Memory&&MemoryPage::back())return true;

 if(app==App::Music&&MusicPage::back())return true;
 if(app==App::Radio&&RadioPage::back())return true;
 if(activePageModule>=0){switch(activePageModule){case 0:if(HomePage::back())return true;break;case 1:if(WeatherPage::back())return true;break;case 3:if(ReadingPage::back())return true;break;case 4:if(DailyPage::back())return true;break;}}
 if(!section.isEmpty()){section="";rerender();return true;}
#ifdef GIFT_HOST_TEST
 if(app!=App::None)return hostAppBack();
#endif
 return false;
}
// Pure query in exactly the same order as back(); desk.cpp's GiftPages::canGoBack()
// wraps it with the App/origin and Wi-Fi keyboard layers.
bool canBack(){
 if((app==App::Alarm||app==App::Pomodoro)&&FocusApps::canBack())return true;
 if(app==App::Timer&&TimerPage::canBack())return true;
 if(app==App::Days&&DaysPage::canBack())return true;
 if(app==App::Tetris&&TetrisPage::canBack())return true;
 if(app==App::Snake&&SnakePage::canBack())return true;
 if(app==App::Jump&&JumpPage::canBack())return true;
 if(app==App::Sheep&&SheepPage::canBack())return true;
 if(app==App::Miner&&MinerPage::canBack())return true;
 if(app==App::Plane&&PlanePage::canBack())return true;
 if(app==App::Breakout&&BreakoutPage::canBack())return true;
 if(app==App::Mines&&MinesPage::canBack())return true;
 if(app==App::Pin&&PinPage::canBack())return true;
 if(app==App::Memory&&MemoryPage::canBack())return true;

 if(app==App::Music&&MusicPage::canBack())return true;
 if(app==App::Radio&&RadioPage::canBack())return true;
 if(activePageModule>=0){switch(activePageModule){case 0:if(HomePage::canBack())return true;break;case 1:if(WeatherPage::canBack())return true;break;case 3:if(ReadingPage::canBack())return true;break;case 4:if(DailyPage::canBack())return true;break;}}
 if(!section.isEmpty())return true;
#ifdef GIFT_HOST_TEST
 if(app!=App::None)return true;
#endif
 return false;
}
String viewKey(){return String((int)app)+":"+String(page)+(app==App::None&&page==6?String(":apps")+String(appsSheet):app==App::Games?String(":games")+String(gamesSheet):app==App::Clock&&section.isEmpty()?String(":clocks")+String(clockSheet):String(""))+":"+section+(app==App::Clock&&section=="clock-face"?String(":")+String(GiftData::get("clockMode")["preset"]|"landscape"):String(""));}
bool render(lv_obj_t *root){
 if(app!=App::Clock)clockSheet=0;
 activePageModule=-1;
 cancelPageAnimations();
 screen=root;savedPage=false;lv_obj_set_user_data(screen,nullptr);hours=minutes=homeDate=weekday=city=temp=condition=monthTitle=monthGrid=battery=nullptr;
 temperatureUnit=wifiCaption=readingBody=woodFeedback=nullptr;
 for(auto &label:weekDays)label=nullptr;holidayAnswer=holidayDay=holidayNew=holidaySpring=nullptr;
 waterLabel=knockLabel=moodSummary=weekMood=waterProgress=nullptr;for(auto &slider:levels)slider=nullptr;for(auto &label:levelLabels)label=nullptr;
 clockTime=clockDate=clockImage=clockPeriod=clockCountdown=nullptr;voiceMessage=quote=poem=moodError=nullptr;holding=false;
 wifiCross=nullptr;for(auto &day:miniDays)day=nullptr;
 weatherIcon=weatherFeels=weatherRange=weatherExtra=weatherUpdated=homeWeatherIcon=homeCondition=homeCity=todayFace=todayPlus=waterWave=quoteDate=quoteAuthor=poemAuthor=nullptr;for(auto &v:metricValues)v=nullptr;for(auto &v:forecastWeek)v=nullptr;for(auto &v:forecastNumbers)v=nullptr;for(auto &v:forecastIcons)v=nullptr;for(auto &v:weekNames)v=nullptr;for(auto &v:weekFaces)v=nullptr;for(auto &v:weekPluses)v=nullptr;for(auto &v:readingSources)v=nullptr;
 lv_obj_set_style_bg_color(screen,lv_color_hex(color(GiftData::get("colors")["bg"],0xf4f7ef)),0);lastRefresh=0;
 if(app==App::None){
  if(section.isEmpty()){
   switch(page){case 0:HomePage::build(screen);activePageModule=0;break;
    case 1:WeatherPage::build(screen);activePageModule=1;break;
    case 3:ReadingPage::build(screen);activePageModule=3;break;
    case 4:DailyPage::build(screen);activePageModule=4;break;
    case 6:appsPage();break;
    default:savedCreate(page);break;}
  }else switch(page){case 3:reading();break;case 4:daily();break;default:savedCreate(page);break;}
 }else if(app==App::Games)gamesPage();else if(app==App::Messages){savedCreate(5);}else if(app==App::Settings)settingsPage();else if(app==App::Clock)clockPage();else if(app==App::Voice)voice();else if(app==App::Weather){WeatherPage::build(screen);activePageModule=1;}else if(app==App::Alarm)FocusApps::buildAlarm(screen);else if(app==App::Pomodoro)FocusApps::buildPomodoro(screen);else if(app==App::Timer)TimerPage::build(screen);else if(app==App::Days)DaysPage::build(screen);else if(app==App::Tetris)TetrisPage::build(screen);else if(app==App::Snake)SnakePage::build(screen);else if(app==App::Jump)JumpPage::build(screen);else if(app==App::Sheep)SheepPage::build(screen);else if(app==App::Miner)MinerPage::build(screen);else if(app==App::Plane)PlanePage::build(screen);else if(app==App::Breakout)BreakoutPage::build(screen);else if(app==App::Mines)MinesPage::build(screen);else if(app==App::Pin)PinPage::build(screen);else if(app==App::Memory)MemoryPage::build(screen);else if(app==App::Music)MusicPage::build(screen);else if(app==App::Radio)RadioPage::build(screen);else return false;
 refresh();return true;
}
void refresh(bool force){
 // status icons follow Wi-Fi/battery even while a designer page is static
 static uint32_t chromeAt=0;uint32_t chromeNow=millis();
 if(chromeNow-chromeAt>=500){chromeAt=chromeNow;Ui::refreshStatusIcons();}
 // Battery sampling still runs at 10Hz, but labels are only rebuilt when the
 // second changes (or while a slider/toast needs immediate feedback). The old
 // 10Hz full refresh re-measured every text and read the JSON store dozens of
 // times per tick, which churned the 112KB internal heap even on static pages.
 static uint32_t powerAt=0;
 if(!screen)return;
 uint32_t nowMs=millis();
 if(nowMs-powerAt>=100){powerAt=nowMs;PowerStatus::poll();}
 bool busy=holding||(woodFeedback&&!lv_obj_has_flag(woodFeedback,LV_OBJ_FLAG_HIDDEN));
 if(!force&&nowMs-lastRefresh<(busy?100:1000))return;
 lastRefresh=nowMs;auto power=PowerStatus::get();
 // Hand-written pages (home / weather / reading / daily) update through their own
 // pointer caches; everything below is the legacy generated-page refresh path.
 if(activePageModule>=0){switch(activePageModule){case 0:HomePage::refresh(force);break;case 1:WeatherPage::refresh(force);break;case 3:ReadingPage::refresh(force);break;case 4:DailyPage::refresh(force);break;}return;}
 if(app==App::Alarm||app==App::Pomodoro){FocusApps::refresh(force);return;}
 if(app==App::Timer){TimerPage::refresh(force);return;}
 if(app==App::Days){DaysPage::refresh(force);return;}
 if(app==App::Tetris){TetrisPage::refresh(force);return;}
 if(app==App::Snake){SnakePage::refresh(force);return;}
 if(app==App::Jump){JumpPage::refresh(force);return;}
 if(app==App::Sheep){SheepPage::refresh(force);return;}
 if(app==App::Miner){MinerPage::refresh(force);return;}
 if(app==App::Plane){PlanePage::refresh(force);return;}
 if(app==App::Breakout){BreakoutPage::refresh(force);return;}
 if(app==App::Mines){MinesPage::refresh(force);return;}
 if(app==App::Pin){PinPage::refresh(force);return;}
 if(app==App::Memory){MemoryPage::refresh(force);return;}

 if(app==App::Music){MusicPage::refresh(force);return;}
 if(app==App::Radio){RadioPage::refresh(force);return;}
 // Cross-day and data-revision rebuilds must be detected before the saved-page
 // early return: the schedule page (5) is a saved page with no refresh branch.
 const String dayKey=GiftData::today();
 if(!inputActive()&&app==App::None&&section.isEmpty()&&page==5&&(lastDay!=dayKey||lastRevision!=GiftData::revision()))renderPending=true;
 lastDay=dayKey;lastRevision=GiftData::revision();
 if(savedPage){savedRefresh();return;}
 if(DashboardClockSettings::active()){DashboardClockSettings::refresh();return;}
 if(GifClockPage::active()||PixelClockPage::active()||FlipClockPage::active()||DashboardClockPage::active()||BarbarClockPage::active()){
  GifClockPage::refresh();PixelClockPage::refresh();FlipClockPage::refresh();DashboardClockPage::refresh();BarbarClockPage::refresh();
  if(drawnPreset!=String(GiftData::get("clockMode")["preset"]|"landscape"))renderPending=true;
  return;
 }
 if(!battery&&!hours&&!monthTitle&&!holidayAnswer&&!levels[0]&&!clockDate&&!clockImage&&!voiceMessage&&!readingBody&&!quote&&!todayFace&&!waterLabel)return;
 text(wifiCaption,WiFi.status()==WL_CONNECTED?"Wi-Fi":"Wi-Fi x");
 if(woodFeedback&&(int32_t)(millis()-woodFeedbackUntil)>=0)hidden(woodFeedback,true);
 if(battery){String value=power.installed?(power.voltageValid?String("约 ")+power.percent+"%":"电量未知"):(power.external?"接电使用":"未接电池");if(power.installed&&power.external)value="接电 · "+value;text(battery,value);textColor(battery,power.low?0xb52a2a:Ui::Ink);}
 int64_t epoch=ClockSource::now();bool valid=DeskLogic::timeValid(epoch);auto d=DeskLogic::beijingDate(epoch);char h[3],m[3];snprintf(h,sizeof(h),"%02d",d.hour);snprintf(m,sizeof(m),"%02d",d.minute);String today=GiftData::today();int actual=dow(d);
 char date[20];snprintf(date,sizeof(date),weatherIcon?"%02d月%02d日 星期":"%02d/%02d 星期",d.month,d.day);const char *englishDays[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
 text(hours,valid?h:"--");text(minutes,valid?m:"--");text(homeDate,valid?String(date)+week[actual]:"等待联网校时");text(weekday,valid?englishDays[actual]:"");
 text(city,settings.city.isEmpty()?"未设置城市":settings.city);String temperature=weather.valid?TextUtil::number(weather.temperature):"--";text(temp,temperature);if(temperatureUnit&&!homeWeatherIcon)lv_obj_set_x(temperatureUnit,80+lv_txt_get_width(temperature.c_str(),temperature.length(),saved_gift_font_bold_34_0,-1,LV_TEXT_FLAG_NONE)+1);text(condition,weather.valid&&!weather.condition.isEmpty()?weather.condition:String(weatherName(weather.valid?weather.code:-1)));
 if(homeWeatherIcon){const auto *src=(!weather.valid||(weather.code<4&&weather.code>0))?&gift_home_icon_cloud:weather.code==0?&gift_home_icon_sun:&gift_home_icon_rain;source(homeWeatherIcon,src);}
 if(weatherIcon){source(weatherIcon,symbol(weather.valid?weather.code:-1));text(weatherFeels,"体感温度 "+TextUtil::number(weather.feels,"°"));text(weatherRange,TextUtil::number(weather.high,"°")+" / "+TextUtil::number(weather.low,"°"));text(weatherExtra,"湿度 "+TextUtil::number(weather.humidity,"%")+" · "+(weather.windText.isEmpty()?TextUtil::number(weather.wind," km/h"):weather.windText));text(weatherUpdated,weatherPending?"更新中":weather.valid?"上次更新 "+TextUtil::formatDate(weather.fetchedAt,"%H:%M"):"上次更新 --");
  String values[]={weather.sunrise,weather.sunset,TextUtil::number(weather.pressure," hPa"),TextUtil::number(weather.uv,""),TextUtil::number(weather.visibility," km"),TextUtil::number(weather.aqi,"")+(!std::isfinite(weather.aqi)?String(""):!weather.aqiCategory.isEmpty()?" "+weather.aqiCategory:weather.aqi<=50?String(" 优"):weather.aqi<=100?String(" 良"):String(""))};const int visibleMetrics[]={0,1,2,5};for(int i=0;i<4;i++)text(metricValues[i],values[visibleMetrics[i]]);
  for(int i=0;i<7;i++){auto f=weather.forecast[i];int y=0,m=0,day=0;bool dated=sscanf(f.date.c_str(),"%d-%d-%d",&y,&m,&day)==3&&DeskLogic::validDate(y,m,day);auto forecast=dated?DeskLogic::beijingDate(DeskLogic::beijingEpoch(y,m,day)):DeskLogic::beijingDate(ClockSource::now()+i*86400);text(forecastWeek[i],valid?"周"+String(week[dow(forecast)]):"--");text(forecastNumbers[i],TextUtil::number(f.high,"°")+" / "+TextUtil::number(f.low,"°"));source(forecastIcons[i],symbol(f.code,true));}
 }
 for(int i=0;i<3;i++)if(weekDays[i]){auto day=DeskLogic::beijingDate(ClockSource::now()+(i-1)*86400);text(weekDays[i],valid?String(day.day):"--");text(weekNames[i],valid?String(week[dow(day)]):"-");}
 if(monthTitle){text(monthTitle,valid?String(d.month)+"月"+d.day+"日":"未校时");int offset=((int)(DeskLogic::civilDays(d.year,d.month,1)+3)%7+7)%7;int weeks=(offset+DeskLogic::daysInMonth(d.year,d.month)+6)/7;for(int i=0;i<42;i++){int day=i-offset+1;bool inMonth=valid&&day>=1&&day<=DeskLogic::daysInMonth(d.year,d.month);text(miniDays[i],inMonth?String(day):"");if(miniDays[i]){lv_obj_set_y(miniDays[i],27+(i/7)*(weeks==6?7:8));textColor(miniDays[i],0xe9eddf);background(miniDays[i],0x8fabcc);if(lv_obj_get_style_radius(miniDays[i],0)!=LV_RADIUS_CIRCLE)lv_obj_set_style_radius(miniDays[i],LV_RADIUS_CIRCLE,0);opacity(miniDays[i],valid&&day==d.day?LV_OPA_COVER:LV_OPA_TRANSP);}}}
 auto holiday=GiftData::get("holiday");int target=holiday["weekday"]|5,remaining=(target-actual+7)%7;
 text(holidayAnswer,!valid?"等待联网校时":remaining==0?String("是的，今天")+(holiday["label"]|"放假"):String("不是，还有")+remaining+"天");
 text(holidayDay,holiday["label"]|"放假");
 int64_t days=DeskLogic::civilDays(d.year,d.month,d.day);int nextYear=d.month==1&&d.day==1?d.year:d.year+1;
 text(holidayNew,valid?String((long)(DeskLogic::civilDays(nextYear,1,1)-days)):"--");
 int springYear=d.year,spring=-1;if(springYear>=1900&&springYear<=2100){int date=GiftSpringDates[springYear-1900];spring=DeskLogic::civilDays(springYear,date/100,date%100)-days;if(spring<0&&springYear<2100){date=GiftSpringDates[++springYear-1900];spring=DeskLogic::civilDays(springYear,date/100,date%100)-days;}}
 text(holidaySpring,valid&&spring>=0?String(spring):"--");
 auto record=GiftData::record(today);int water=waterTotal(record),goal=GiftData::get("daily")["waterGoal"]|1000;
 text(knockLabel,section=="wood"?String("今日 ")+String(record["knocks"]|0)+" 次":String(record["knocks"]|0)+" 次");text(waterLabel,section=="water"?String("今天 ")+water+" ml · 目标 "+goal+" ml":String(water)+"ml");text(waterProgress,String(std::min(999,water*100/goal))+"% · "+goal+"ml");if(waterWave)raiseWater((lv_coord_t)(91-std::min(100,water*100/goal)*55/100));
 if(todayFace){String mood=record["mood"]|"";source(todayFace,moodImage(mood));hidden(todayFace,mood.isEmpty());hidden(todayPlus,!mood.isEmpty());int monday=(actual+6)%7;for(int i=0;i<7;i++){auto day=DeskLogic::beijingDate(ClockSource::now()+(i-monday)*86400);String face=GiftData::record(dateKey(day))["mood"]|"";source(weekFaces[i],moodImage(face,true));hidden(weekFaces[i],face.isEmpty());hidden(weekPluses[i],!face.isEmpty()||i>monday);}}
 if(weekMood){String summary;for(int i=-6;i<=0;i++){auto date=DeskLogic::beijingDate(ClockSource::now()+i*86400);auto item=GiftData::record(dateKey(date));const char *mood=item["mood"]|"";summary+=String(week[dow(date)])+":"+(strlen(mood)?"有":"-")+"  ";}text(weekMood,summary);}
 for(int i=0;i<2;i++)if(levels[i]){int value=GiftData::get("deviceSettings")[i==0?"brightness":"volume"]|45;if(!holding)lv_slider_set_value(levels[i],value,LV_ANIM_OFF);text(levelLabels[i],String(value)+"%"+(i==1&&value==0?" 静音":""));}
 if(clockDate){char s[3];snprintf(s,sizeof(s),"%02d",d.second);String clock=valid?String(h)+":"+m+":"+s:"--:--:--";text(clockTime,clock);
   if(clockCountdown)updateCountdown(valid,d);
   text(clockPeriod,valid?(d.hour>=12?"PM":"AM"):"--");text(clockDate,valid?String(d.year)+"年 "+d.month+"月 "+d.day+"日 周"+week[actual]:"等待联网校时");
   if(drawnPreset!=String(GiftData::get("clockMode")["preset"]|"landscape"))renderPending=true;
 }
 if(voiceMessage)text(voiceMessage,XiaozhiVoice::get().message);
 if(readingBody){String value=readingBodyText(section=="reading-quote"?"quote":"poem");if(value!=readingRaw){readingRaw=value;text(readingBody,value);}}if(quote)text(quote,readingValue("quote","Text"));if(poem){String verse=readingValue("poem","Text");verse.replace("，\n","，");verse.replace("。\n","。");verse.replace("，","，\n");verse.replace("。","。\n");while(!verse.isEmpty()&&verse[verse.length()-1]=='\n')verse=verse.substring(0,verse.length()-1);text(poem,verse);}text(quoteAuthor,readingValue("quote","Author"));String dynasty=readingValue("poem","Dynasty");text(poemAuthor,(dynasty.isEmpty()?String(""):dynasty+"代 · ")+readingValue("poem","Author"));char quoteDateText[6];snprintf(quoteDateText,sizeof(quoteDateText),"%02d/%02d",d.month,d.day);text(quoteDate,valid?quoteDateText:"--/--");
 for(int i=0;i<2;i++)if(readingSources[i]){const char *kind=i==0?"quote":"poem";text(readingSources[i],GiftData::get("reading")[String(kind)+"Mode"]=="manual"?"自定义":GiftData::get("readingCache")[kind].isNull()?"示例":GiftData::get("readingCache")[kind]["date"]==today?"今日":"缓存");}
}
// ---- cached home view (P1-3) ---------------------------------------------
// Lifetime of the cache:
//   * desk.cpp creates the permanent homeLayer/pageLayer once and calls
//     buildHome() the first time the home view is shown;
//   * from then on returning home only calls refreshHome(), which addresses the
//     widgets cached in HomeRefs instead of walking SavedDesign's name index;
//   * the cache is never rebuilt (the generated home page is static, all live
//     data is covered by HomeRefs), so it stays valid until reboot;
//   * render() keeps its old behaviour, so the host test runner is unaffected.
namespace {
bool homeBuilt = false;

void captureHomeRefs(HomeRefs &refs) {
    refs = HomeRefs{};
    refs.layer = screen;
    refs.clockHour = savedFind("clock_hour");
    refs.clockMinute = savedFind("clock_minute");
    refs.clockDate = savedFind("clock_date");
    refs.clockWeekday = savedFind("clock_weekday");
    refs.homeTemperature = savedFind("home_temperature");
    refs.homeTemperatureUnit = savedFind("home_temperature_unit");
    refs.homeTemperatureUnitText = savedFind("home_temperature_unit_text");
    refs.homeCity = savedFind("home_city");
    refs.homeCondition = savedFind("home_condition");
    refs.calendarTitle = savedFind("calendar_title");
    refs.calendarToday = savedFind("calendar_today");
    for (int day = 1; day <= 31; ++day) {
        char name[24];
        snprintf(name, sizeof(name), "calendar_%d", day);
        refs.calendar[day - 1] = savedFind(name);
    }
    if (refs.calendar[0]) {
        lv_obj_t *parent = lv_obj_get_parent(refs.calendar[0]);
        const lv_font_t *font = lv_obj_get_style_text_font(refs.calendar[0], 0);
        for (int i = 0; i < 11; ++i) {
            lv_obj_t *cell = lv_label_create(parent);
            lv_obj_set_size(cell, 14, 9);
            lv_obj_set_style_text_font(cell, font, 0);
            lv_obj_set_style_text_color(cell, lv_color_hex(0xE9EDDF), 0);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            refs.calendarExtra[i] = cell;
        }
        if (refs.calendarToday) lv_obj_move_foreground(refs.calendarToday);
    }
    for (int i = 0; i < 3; ++i) {
        char name[24];
        snprintf(name, sizeof(name), "week_day_%d", i);
        refs.weekDay[i] = savedFind(name);
        snprintf(name, sizeof(name), "week_number_%d", i);
        refs.weekNumber[i] = savedFind(name);
    }
    refs.statusWifi = savedFind("image_1");
    refs.statusBt = savedFind("image_2");
    refs.statusBattery = savedFind("image_3");
    refs.statusOnline = savedFind("label_3");
    refs.statusBtLabel = savedFind("label_4");
    refs.statusPercent = savedFind("label_6");
    refs.text79 = savedFind("text_79");
    refs.icon80 = savedFind("icon_80");
    refs.icon85 = savedFind("icon_85");
    refs.powerStatus = savedFind("power_status");
    refs.wifiCross = wifiCross;
    // Clock/calendar/temperature are what the 1 Hz refresh cannot work without;
    // if the design ever loses them the caller keeps the rebuilding path.
    refs.valid = refs.clockHour && refs.clockMinute && refs.calendarTitle && refs.homeTemperature;
}

// Pointer version of the savedRefresh() savedIndex==0 branch. Keep the two in
// sync: same label writes, geometry fixes and icon swaps, only resolved through
// HomeRefs instead of savedFind(). No lv_obj_add_style() here (1 Hz path).
static void fitHomeIcon(lv_obj_t *icon,int width,int height){
 const lv_img_dsc_t *src=(const lv_img_dsc_t*)lv_img_get_src(icon);
 if(!src||!src->header.w||!src->header.h)return;
 int zoomW=(width*256)/src->header.w,zoomH=(height*256)/src->header.h;
 int zoom=zoomW<zoomH?zoomW:zoomH; lv_img_set_zoom(icon,(uint16_t)zoom);
}
// Text-only refresh for the latest design: updates values without touching the
// designer geometry (the old fit/resize code assumed the previous layout).
void refreshHomeLive(const HomeRefs &refs) {
 static uint32_t last=0; uint32_t nowMs=millis(); if(nowMs-last<1000)return; last=nowMs;
 int64_t now=ClockSource::now(); bool valid=DeskLogic::timeValid(now);
 auto d=DeskLogic::beijingDate(now); int actual=dow(d); char buf[40];
 if(refs.clockHour){snprintf(buf,sizeof(buf),valid?"%02d":"--",d.hour);text(refs.clockHour,buf);}
 if(refs.clockMinute){snprintf(buf,sizeof(buf),valid?"%02d":"--",d.minute);text(refs.clockMinute,buf);}
 if(refs.clockDate){snprintf(buf,sizeof(buf),"%02d/%02d 星期%s",d.month,d.day,week[actual]);text(refs.clockDate,valid?buf:"等待联网校时");}
 if(refs.clockWeekday){const char *days[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};text(refs.clockWeekday,valid?days[actual]:"");}
 if(refs.homeTemperature)text(refs.homeTemperature,weather.valid?TextUtil::number(weather.temperature):"--");
 if(refs.homeCondition)text(refs.homeCondition,weather.valid?(weather.condition.isEmpty()?String(weatherName(weather.code)):weather.condition):"暂无");
 if(refs.homeCity)text(refs.homeCity,settings.city.isEmpty()?"未设置城市":settings.city);
 const bool online=WiFi.status()==WL_CONNECTED; auto power=PowerStatus::get();
 if(refs.statusWifi)setDesignerIcon(refs.statusWifi,online?saved_gift_status_wifi:saved_gift_status_offline,17,17);
 if(refs.statusBattery)setDesignerIcon(refs.statusBattery,power.external?saved_gift_status_battery:(power.low?saved_gift_status_battery_low:saved_gift_status_battery_fill),29,18);
 if(refs.statusOnline)text(refs.statusOnline,online?"Online":"离线");
 if(refs.statusBtLabel)text(refs.statusBtLabel,"BT ON");
 if(refs.statusPercent){char percentText[16];snprintf(percentText,sizeof(percentText),"%d %%",(power.installed&&power.voltageValid)?power.percent:-1);text(refs.statusPercent,power.voltageValid?String(percentText):String("—"));}
 if(refs.calendarTitle)text(refs.calendarTitle,valid?String(d.month)+"月"+String(d.day)+"日":"未校时");
 if(refs.calendar[0]){
  lv_obj_t *cells[42];
  for(int i=0;i<31;i++)cells[i]=refs.calendar[i];
  for(int i=0;i<11;i++)cells[31+i]=refs.calendarExtra[i];
  int offset=((int)(DeskLogic::civilDays(d.year,d.month,1)+3)%7+7)%7;
  int daysInMonth=DeskLogic::daysInMonth(d.year,d.month);
  int prevMonth=d.month==1?12:d.month-1;
  int prevYear=d.month==1?d.year-1:d.year;
  int prevDays=DeskLogic::daysInMonth(prevYear,prevMonth);
  int trailing=1;
  int rows=(offset+daysInMonth+6)/7;
  int step=rows==6?7:8;
  for(int slot=0;slot<42;slot++){
   int day=0;bool inMonth=false;
   if(slot>=offset+daysInMonth){lv_obj_t *emptyCell=cells[slot];if(emptyCell)lv_obj_add_flag(emptyCell,LV_OBJ_FLAG_HIDDEN);continue;}
   if(slot<offset)day=prevDays-offset+slot+1;
   else if(slot<offset+daysInMonth){day=slot-offset+1;inMonth=true;}
   else day=trailing++;
   lv_obj_t *cell=cells[slot];
   if(!cell)continue;
   if(day<1||day>31){lv_obj_add_flag(cell,LV_OBJ_FLAG_HIDDEN);continue;}
   int col=slot%7,row=slot/7;
   lv_obj_set_pos(cell,6+(col*82+3)/6,32+row*step);
   lv_obj_clear_flag(cell,LV_OBJ_FLAG_HIDDEN);
   lv_label_set_text_fmt(cell,"%d",day);
   lv_obj_set_style_text_color(cell,lv_color_hex(inMonth?(day==d.day?0xFFFFFF:0xE9EDDF):0x7F9078),0);
   if(valid&&inMonth&&day==d.day&&refs.calendarToday){
    lv_obj_set_pos(refs.calendarToday,6+(col*82+3)/6+3,32+row*step-1);
    lv_obj_clear_flag(refs.calendarToday,LV_OBJ_FLAG_HIDDEN);
   }
  }
  for(int slot=0;slot<42;slot++)if(cells[slot]&&!lv_obj_has_flag(cells[slot],LV_OBJ_FLAG_HIDDEN))lv_obj_move_foreground(cells[slot]);
  if(!valid&&refs.calendarToday)lv_obj_add_flag(refs.calendarToday,LV_OBJ_FLAG_HIDDEN);
 }
 for(int i=0;i<3;i++){auto other=DeskLogic::beijingDate(now+(i-1)*86400);
  if(refs.weekDay[i])text(refs.weekDay[i],valid?week[dow(other)]:"-");
  if(refs.weekNumber[i])text(refs.weekNumber[i],valid?String(other.day):"--");}
}

void refreshHomeData(const HomeRefs &refs) {
 if(kDesignStatic)return;
    int64_t now = ClockSource::now();
    bool valid = DeskLogic::timeValid(now);
    auto d = DeskLogic::beijingDate(now);
    int actual = dow(d);
    char value[80];
    // The layer stays transparent; desk.cpp mirrors this colour onto the screen
    // object, where the design background used to be painted.
    if (refs.layer) background(refs.layer, color(GiftData::get("colors")["bg"], 0xf4f7ef));
    snprintf(value, sizeof(value), "%02d", d.hour);
    text(refs.clockHour, valid ? value : "--");
    snprintf(value, sizeof(value), "%02d", d.minute);
    text(refs.clockMinute, valid ? value : "--");
    snprintf(value, sizeof(value), "%02d/%02d 星期%s", d.month, d.day, week[actual]);
    text(refs.clockDate, valid ? value : "等待联网校时");
    const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    text(refs.clockWeekday, valid ? days[actual] : "");
    static const lv_font_t *homeTempFonts[] = {saved_gift_yahei_b_17_0, saved_gift_font_bold_14_0, &fd_sans_bold_12};
    savedShrinkLine(refs.homeTemperature, weather.valid ? TextUtil::number(weather.temperature) : "--", 48, 21, homeTempFonts, 3);
    if (refs.homeTemperature && refs.homeTemperatureUnit) {
        auto *unit = refs.homeTemperatureUnit;
        auto *unitText = refs.homeTemperatureUnitText;
        const auto *numberFont = lv_obj_get_style_text_font(refs.homeTemperature, 0);
        const auto *unitFont = unitText ? lv_obj_get_style_text_font(unitText, 0) : nullptr;
        if (!numberFont || !unitFont) unitFont = saved_gift_font_bold_10_0;
        int measuredWidth = savedMeasuredWidth(refs.homeTemperature);
        int x = measuredWidth + 1;
        int y = numberFont->line_height - numberFont->base_line - unitFont->line_height + unitFont->base_line;
        if (lv_obj_get_style_x(unit, 0) != x || lv_obj_get_style_y(unit, 0) != y) lv_obj_set_pos(unit, x, y);
    }
    static const lv_font_t *homeCityFonts[] = {saved_gift_font_bold_10_0, saved_gift_font_bold_9_0, saved_gift_font_bold_8_0};
    savedShrinkLine(refs.homeCity, settings.city.isEmpty() ? "未设置城市" : settings.city, 72, 12, homeCityFonts, 3);
    static const lv_font_t *homeConditionFonts[] = {saved_gift_font_bold_11_0, saved_gift_font_bold_10_0, saved_gift_font_bold_9_0};
    savedShrinkLine(refs.homeCondition, weather.valid ? (weather.condition.isEmpty() ? String(weatherName(weather.code)) : weather.condition) : "暂无", 72, 13, homeConditionFonts, 3);
    textColor(refs.homeCondition, 0xFFFFFF);
    textColor(refs.homeCity, 0xEDF5D8);
    text(refs.calendarTitle, valid ? String(d.month) + "月" + d.day + "日" : "未校时");
    int offset = ((DeskLogic::civilDays(d.year, d.month, 1) + 3) % 7 + 7) % 7;
    int daysInMonth = DeskLogic::daysInMonth(d.year, d.month);
    int rows = (offset + daysInMonth + 6) / 7;
    int step = rows == 6 ? 7 : 8;
    for (int day = 1; day <= 31; ++day) {
        auto *obj = refs.calendar[day - 1];
        hidden(obj, !valid || day > daysInMonth);
        if (!obj || !valid || day > daysInMonth) continue;
        int slot = offset + day - 1;
        int x = (slot % 7 * 96 + 3) / 7, y = slot / 7 * step;
        if (lv_obj_get_style_x(obj, 0) != x || lv_obj_get_style_y(obj, 0) != y) lv_obj_set_pos(obj, x, y);
        textColor(obj, day == d.day ? 0xffffff : 0xe9eddf);
        if (day == d.day && refs.calendarToday) {
            if (lv_obj_get_style_x(refs.calendarToday, 0) != x + 2 || lv_obj_get_style_y(refs.calendarToday, 0) != y - 1)
                lv_obj_set_pos(refs.calendarToday, x + 2, y - 1);
        }
    }
    hidden(refs.calendarToday, !valid);
    for (int i = 0; i < 3; ++i) {
        auto other = DeskLogic::beijingDate(now + (i - 1) * 86400);
        text(refs.weekDay[i], valid ? week[dow(other)] : "-");
        text(refs.weekNumber[i], valid ? String(other.day) : "--");
    }
    auto power = PowerStatus::get();
    bool online = WiFi.status() == WL_CONNECTED;
    text(refs.text79, "Wi-Fi");
    if (refs.icon80) lv_obj_set_style_img_opa(refs.icon80, online ? 255 : 70, 0);
    hidden(refs.wifiCross, online);
    if (refs.icon85) {
        auto *icon = refs.icon85;
        if (power.chargingKnown && power.charging) {
            lv_img_set_src(icon, saved_gift_charge);
            lv_obj_set_size(icon, 16, 11);
            lv_obj_set_pos(icon, 0, 4);
            // 1 Hz path: never lv_obj_add_style() here (it grows the style list
            // without bound and froze the loop). Local properties are idempotent.
            lv_obj_set_style_img_recolor(icon, lv_color_hex(0x526642), 0);
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
        } else {
            lv_img_set_src(icon, saved_gift_battery);
            lv_obj_set_size(icon, 15, 12);
            lv_obj_set_pos(icon, 0, 3);
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_TRANSP, 0);
        }
    }
    String status;
    if (power.installed) status = power.voltageValid ? "约 " + String(power.percent) + "%" : "电量未知";
    hidden(refs.powerStatus, status.isEmpty());
    text(refs.powerStatus, status);
}
}  // namespace

void buildHome(lv_obj_t *layer, HomeRefs &refs) {
    // Same reset contract as render(): no pointer from a previous view may
    // survive, otherwise a later page render could dereference the cached tree.
    cancelPageAnimations();
    screen = layer;
    savedPage = false;
    lv_obj_set_user_data(screen, nullptr);
    hours = minutes = homeDate = weekday = city = temp = condition = monthTitle = monthGrid = battery = nullptr;
    temperatureUnit = wifiCaption = readingBody = woodFeedback = nullptr;
    for (auto &label : weekDays) label = nullptr;
    holidayAnswer = holidayDay = holidayNew = holidaySpring = nullptr;
    waterLabel = knockLabel = moodSummary = weekMood = waterProgress = nullptr;
    for (auto &slider : levels) slider = nullptr;
    for (auto &label : levelLabels) label = nullptr;
    clockTime = clockDate = clockImage = clockPeriod = clockCountdown = nullptr;
    voiceMessage = quote = poem = moodError = nullptr;
    holding = false;
    wifiCross = nullptr;
    for (auto &day : miniDays) day = nullptr;
    weatherIcon = weatherFeels = weatherRange = weatherExtra = weatherUpdated = homeWeatherIcon = homeCondition = homeCity = todayFace = todayPlus = waterWave = quoteDate = quoteAuthor = poemAuthor = nullptr;
    for (auto &v : metricValues) v = nullptr;
    for (auto &v : forecastWeek) v = nullptr;
    for (auto &v : forecastNumbers) v = nullptr;
    for (auto &v : forecastIcons) v = nullptr;
    for (auto &v : weekNames) v = nullptr;
    for (auto &v : weekFaces) v = nullptr;
    for (auto &v : weekPluses) v = nullptr;
    for (auto &v : readingSources) v = nullptr;
    lv_obj_set_style_bg_color(screen, lv_color_hex(color(GiftData::get("colors")["bg"], 0xf4f7ef)), 0);
    lastRefresh = 0;
    SavedDesign::invalidate();
    // The cached home is the hand-written page module; it caches all widget
    // pointers itself, so HomeRefs only carries the cache-state flag.
    activePageModule = 0;
    HomePage::build(screen);
    refs = HomeRefs{};
    refs.layer = screen;
    refs.valid = HomePage::ready();
    homeBuilt = refs.valid;
    HomePage::refresh(true);  // first frame, filled before desk.cpp flushes it
    lastRefresh = millis();
}

void refreshHome(const HomeRefs &refs, bool force) {
    static uint32_t powerAt = 0;
    static uint32_t statusAt = 0;
    if (!refs.valid) return;
    uint32_t nowMs = millis();
    if (nowMs - powerAt >= 100) {
        powerAt = nowMs;
        PowerStatus::poll();
    }
    // Battery-only refresh (A5): the <=5% blink keeps its 500 ms phase instead of
    // being sampled at the 1 Hz data cadence below.
    if (nowMs - statusAt >= 500) {
        statusAt = nowMs;
        HomePage::refreshStatus();
    }
    if (!force && nowMs - lastRefresh < 1000) return;
    lastRefresh = nowMs;
    // The module keeps its own pointer cache; the gate above already applied the
    // 1 Hz throttle, so hand it a forced update.
    HomePage::refresh(true);
}

bool homeCached() { return homeBuilt; }

}
