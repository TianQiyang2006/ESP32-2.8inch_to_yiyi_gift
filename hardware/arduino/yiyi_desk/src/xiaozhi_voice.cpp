#include "xiaozhi_voice.h"
#include "gift_data.h"
#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

// The desktop launches the official application. Voice/UI/protocol run there.
namespace XiaozhiVoice {
namespace {
Snapshot status;
bool pending=false;
const esp_partition_t *officialPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,"app1");
}
String installedVersion() {
    auto partition=officialPartition();esp_app_desc_t desc{};
    if(!partition||esp_ota_get_partition_description(partition,&desc)!=ESP_OK||
       String(desc.project_name)!="xiaozhi")return {};
    String version(desc.version);
    return version.startsWith("2.1.0")||version.startsWith("2.5.0")||version=="2.5.3"?version:String();
}
bool installed(){return !installedVersion().isEmpty();}
String implementation(){auto version=installedVersion();return version.isEmpty()?String("official"):String("official-")+version;}
}
void begin(){status.message=installed()?"正在打开官方小智…":"尚未安装官方小智固件";}
void launch(){pending=true;status.stage=Stage::Connecting;status.message="正在打开官方小智…";}
void poll(){
    if(!pending)return;pending=false;
    if(!installed()){status.stage=Stage::Error;status.message="官方小智固件未安装，返回后重新烧录双应用固件";return;}
    if(WiFi.status()!=WL_CONNECTED&&installedVersion()!="2.5.3"){status.stage=Stage::Error;status.message="请先在桌面设置中连接 Wi-Fi，再打开小智";return;}
    Preferences shared;shared.begin("yiyi-desk",false);shared.putBool("voiceReturn",true);shared.end();
    Preferences audio;audio.begin("audio",false);audio.putInt("output_volume",GiftData::get("deviceSettings")["volume"]|45);audio.end();
    Preferences display;display.begin("display",false);display.putInt("brightness",GiftData::get("deviceSettings")["brightness"]|100);display.end();
    esp_err_t result=esp_ota_set_boot_partition(officialPartition());
    if(result!=ESP_OK){status.stage=Stage::Error;status.message="小智启动失败："+String(esp_err_to_name(result));return;}
    Serial.println("Starting official Xiaozhi application");Serial.flush();ESP.restart();
}
void talk(){launch();}
void stopListening(){}
void close(){pending=false;status.stage=Stage::Idle;}
Snapshot get(){return status;}
bool busy(){return pending;}
void settings(JsonDocument &out){out["implementation"]=implementation();out["installed"]=installed();out["message"]="Wi-Fi 在桌面设置中连接，绑定由官方小智管理";}
void diagnostics(JsonDocument &out){out["implementation"]=implementation();out["installed"]=installed();out["pending"]=pending;}
bool configure(JsonVariantConst,String &error){error="请从全部功能进入官方小智完成绑定";return false;}
}
