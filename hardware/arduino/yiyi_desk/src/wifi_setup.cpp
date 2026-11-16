#include "wifi_setup.h"
#include "app_state.h"
#include "ui_widgets.h"
#include "gift_assets/gift_fonts.h"
#include "touch.h"   // K2：键盘抬手触发后，用 Touch::swiped() 挡掉“刚滑过”的抬手输入
#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <vector>

namespace WifiSetup {
namespace {
struct Network { String ssid; int rssi; bool secure; };
std::vector<Network> networks;
std::vector<Network> pendingNetworks;
SaveNetwork save_ = nullptr; RefreshView refresh_ = nullptr; ResumeStation resume_ = nullptr;
String accessPoint, accessPassword;
bool active = false, editing = false, connecting = false;
std::atomic<bool> scanning{false};
std::atomic<bool> scanDone{false};
std::atomic<int> scanResult{WIFI_SCAN_FAILED};
// The scan worker runs the blocking scan only; dropping/restoring the access
// point and resuming the station all happen on the loop thread.
std::atomic<bool> scanDroppedAp{false};
std::atomic<bool> resumePending{false};
uint32_t started = 0; wl_status_t previous = WL_IDLE_STATUS;
String ssid, password, error;
lv_obj_t *passwordInput = nullptr, *ssidInput = nullptr, *keyMatrix = nullptr, *activeInput = nullptr;
lv_obj_t *statusLabel = nullptr, *modeButton = nullptr, *backspaceButton = nullptr;
int keyMode = 0;  // 0 lower, 1 upper, 2 symbols
// K2 长按状态：LVGL 长按抬手后仍会补发一次 CLICKED，用标记吞掉那一次，
// 避免退格多删一个字符、模式按钮又循环回小写页。
bool backspaceLongPress = false, modeLongPress = false;
// K2 聚焦边框：渲染时从新建输入框读取主题默认边框，失焦时原样还原（不改尺寸/坐标）。
// 只给上/左/右三边着色：输入框的实际底边落在命令行 y=92 后面（布局既有重叠），
// 给底边也上色会在按钮间隙露出绿色短线（设备 golden 差异 18px），故底边保持主题色。
lv_color_t inputBorderColor;
lv_coord_t inputBorderWidth = 1;
lv_border_side_t inputBorderSide = LV_BORDER_SIDE_FULL;
void connectEvent(lv_event_t *);
void editEvent(lv_event_t *);
void backspaceEvent(lv_event_t *);
void modeEvent(lv_event_t *);
void applyKeyMap();
// The stock lv_keyboard draws LV_SYMBOL_* keys, but none of the fonts used on
// this board contains those glyphs (they render as blank keys) and its four rows
// do not fit the 320x240 page. A plain button matrix with ASCII labels is used
// instead; backspace/mode/save/return are real buttons above it.
// All three maps have 4 rows of 10 width units (lower/upper = 10 keys per row;
// K2 symbols R4 = 8 keys, where "SPACE" occupies 3 width units via ctrl_map).
const char *const kbMapLower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "-", "_", ""};
const char *const kbMapUpper[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "\n",
    "Z", "X", "C", "V", "B", "N", "M", ".", "-", "_", ""};
// K2（design/WiFi配网键盘-设计稿-20261028.md §4.3）：符号页只重排第 4 行——
// 首键改成可见的 "SPACE"（宽度由 ctrl_map 指定为 3 个单位），其余 7 键是 Wi-Fi
// 密码最常用的 @ . - _ , : ;（与 R1/R2 的重复是有意的快捷位）。
// R1–R3 与现状完全一致，避免改变常用符号的位置记忆；键帽只用 Montserrat 14 可显示的 ASCII。
const char *const kbMapSymbols[] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "-", "_", "=", "+", "[", "]", "{", "}", ",", ".", "\n",
    "/", "?", ":", ";", "<", ">", "~", "`", "|", "'", "\n",
    "SPACE", "@", ".", "-", "_", ",", ":", ";", ""};
const char *const *keyMapForMode() { return keyMode == 1 ? kbMapUpper : keyMode == 2 ? kbMapSymbols : kbMapLower; }
// ---- K2：键盘 ctrl_map（抬手触发 + 符号页 SPACE 三倍宽） -------------------
// LVGL 8.4 的 btnmatrix 默认 ctrl_bits=0，VALUE_CHANGED 在“按下”时派发（审计 02 P0）；
// 这里给所有键加 LV_BTNMATRIX_CTRL_CLICK_TRIG，改成抬手才派发，滑动误触时手指划过的键
// 不再插入字符。宽度占用 ctrl_bits 的低 4 位（LVGL 8.4 内部 LV_BTNMATRIX_WIDTH_MASK=0xF），
// 所以符号页 R4 首键 "SPACE" = CLICK_TRIG | 3（3 个宽度单位）。
// 注意：set_map 在键数变化时（小写/大写 40 键 vs 符号 38 键）会重新分配并清零 ctrl_bits，
// 因此每次换页都必须 set_map 之后再 set_ctrl_map。
lv_btnmatrix_ctrl_t lowerCtrl[40], upperCtrl[40], symbolCtrl[38];
bool ctrlMapsReady = false;
void initCtrlMaps() {
    if (ctrlMapsReady) return;
    for (auto &value : lowerCtrl) value = LV_BTNMATRIX_CTRL_CLICK_TRIG;
    for (auto &value : upperCtrl) value = LV_BTNMATRIX_CTRL_CLICK_TRIG;
    for (auto &value : symbolCtrl) value = LV_BTNMATRIX_CTRL_CLICK_TRIG;
    symbolCtrl[30] = LV_BTNMATRIX_CTRL_CLICK_TRIG | 3;  // R4 首键 "SPACE"（3 单位宽）
    ctrlMapsReady = true;
}
// K2：LVGL 8.4 没有 per-object 的重复间隔 API；长按重复周期来自触摸 indev 的
// driver->long_press_repeat_time（默认 100ms）。键盘页临时设为 90ms（退格连删 =
// 400ms 后每 90ms 一个字符），leave()/列表页恢复默认；全固件只有退格注册了 REPEAT。
void applyKeyboardLongPressTiming(uint16_t repeatMs) {
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        indev->driver->long_press_repeat_time = repeatMs;
    }
}
// 切换键位页：set_map -> （键数变化后必须）set_ctrl_map -> 更新模式按钮 label。
void applyKeyMap() {
    if (keyMatrix) {
        const char *const *map = keyMapForMode();
        lv_btnmatrix_set_map(keyMatrix, (const char **)map);
        initCtrlMaps();
        lv_btnmatrix_set_ctrl_map(keyMatrix, keyMode == 2 ? symbolCtrl : (keyMode == 1 ? upperCtrl : lowerCtrl));
    }
    if (modeButton) {
        auto *label = lv_obj_get_child(modeButton, 0);
        if (label) { static const char *names[] = {"小写", "大写", "符号"}; lv_label_set_text(label, names[keyMode]); }
    }
    if (activeInput) lv_obj_add_state(activeInput, LV_STATE_FOCUSED);
}
// Access-point recovery state (loop thread only). Restoration is a short
// poll-driven state machine instead of vTaskDelay() so LVGL never stalls.
uint8_t restoreStage = 0;  // 0 idle, 1 radio off settle, 2 AP+STA settle, 3 softAP attempts
uint32_t restoreAt = 0, restoreNextAt = 0;
uint8_t restoreAttempts = 0;
uint32_t apCheckedAt = 0, apRetryAt = 0;
uint8_t apFailures = 0;
uint32_t apRebuilds = 0;
uint16_t apRestoreFailures = 0;
bool apReportedDown = false;
void redraw() { if (active && refresh_) refresh_(); }
// The ESP32 Arduino scan wrapper fails in AP+STA mode while the station is not
// connected (espressif/arduino-esp32#8916, and this board reproduces it as
// ESP_ERR_WIFI_STATE). The configuration screen is used exactly in that state,
// so temporarily drop to STA-only while scanning, then restore the access point.
// A blocking scan in a worker task keeps the LVGL loop responsive.
void scanWorker(void *) {
    // The station-only settle delay keeps the scan wrapper from failing with
    // ESP_ERR_WIFI_STATE while the access point is down (espressif/arduino-esp32#8916).
    // The loop thread has already dropped the AP before starting this task; this
    // task only scans, every WiFi configuration call stays on the loop thread.
    if (scanDroppedAp.load()) vTaskDelay(pdMS_TO_TICKS(320));
    bool previousSleep = WiFi.getSleep();
    WiFi.setSleep(false);
    int result = WiFi.scanNetworks(false, true, false, 300);
    std::vector<Network> found;
    if (result > 0) {
        for (int index = 0; index < result && found.size() < 30; ++index) {
            String name = WiFi.SSID(index);
            if (!name.isEmpty()) found.push_back({name, WiFi.RSSI(index), WiFi.encryptionType(index) != WIFI_AUTH_OPEN});
        }
    }
    WiFi.scanDelete();
    WiFi.setSleep(previousSleep);
    Serial.printf("WIFI scan status=%d result=%d count=%u\n", (int)WiFi.status(), result, (unsigned)found.size());
    pendingNetworks.swap(found);
    scanResult.store(result < 0 ? WIFI_SCAN_FAILED : (int)pendingNetworks.size());
    scanDone.store(true);
    vTaskDelete(nullptr);
}
void feedback() {
    if (!statusLabel) return;
    String value = connecting ? "正在连接…" : error;
    if (!connecting && error.isEmpty() && WiFi.status()==WL_CONNECTED) value="已连接："+WiFi.SSID();
    lv_label_set_text(statusLabel,value.c_str());
}
// K2：聚焦的输入框加 1px #7FA97F 边框；失焦的输入框还原主题默认边框色/宽度/边。
// 只改样式，不改坐标/尺寸；默认边框属性在 render() 建输入框时读取，保证失焦像素不变。
void applyInputBorder(lv_obj_t *input, bool focused) {
    if (!input) return;
    if (focused) {
        lv_obj_set_style_border_color(input, lv_color_hex(0x7FA97F), 0);
        lv_obj_set_style_border_width(input, 1, 0);
        lv_obj_set_style_border_side(input, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
    } else {
        lv_obj_set_style_border_color(input, inputBorderColor, 0);
        lv_obj_set_style_border_width(input, inputBorderWidth, 0);
        lv_obj_set_style_border_side(input, inputBorderSide, 0);
    }
}
void setActiveInput(lv_obj_t *input) {
    if (activeInput == input) return;
    applyInputBorder(activeInput, false);
    if (activeInput) lv_obj_clear_state(activeInput, LV_STATE_FOCUSED);
    activeInput = input;
    applyInputBorder(activeInput, true);
    if (activeInput) {
        lv_obj_add_state(activeInput, LV_STATE_FOCUSED);
        lv_textarea_set_cursor_pos(activeInput, LV_TEXTAREA_CURSOR_LAST);
    }
}
void editEvent(lv_event_t *event) {
    lv_obj_t *target=lv_event_get_target(event);
    const lv_event_code_t code=lv_event_get_code(event);
    if (code==LV_EVENT_VALUE_CHANGED) {
        if(target==passwordInput)password=lv_textarea_get_text(target);
        else if(target==ssidInput)ssid=lv_textarea_get_text(target);
    } else if (code==LV_EVENT_FOCUSED || code==LV_EVENT_CLICKED) {
        setActiveInput(target);
    }
}
// K2：单击循环 小写→大写→符号（与现状一致）；长按直接跳到符号页。
// 长按抬手后 LVGL 仍会补发一个 CLICKED，用 modeLongPress 吞掉，避免又循环回小写。
void modeEvent(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESS_LOST) { modeLongPress = false; return; }
    if (code == LV_EVENT_LONG_PRESSED) {
        modeLongPress = true;
        keyMode = 2;
        applyKeyMap();
        return;
    }
    if (code != LV_EVENT_CLICKED) return;
    if (modeLongPress) { modeLongPress = false; return; }
    keyMode = (keyMode + 1) % 3;
    applyKeyMap();
}
void deleteLastChar() {
    if (!activeInput) return;
    lv_textarea_del_char(activeInput);
    lv_textarea_set_cursor_pos(activeInput, LV_TEXTAREA_CURSOR_LAST);
}
// K2：单击删 1 个字符；长按 400ms 后按重复周期（键盘页 90ms）连删，复用同一删除函数。
// 长按抬手补发的 CLICKED 用 backspaceLongPress 吞掉，避免多删一个字符。
void backspaceEvent(lv_event_t *event) {
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESS_LOST) { backspaceLongPress = false; return; }
    if (code == LV_EVENT_LONG_PRESSED) {
        backspaceLongPress = true;
        deleteLastChar();   // 400ms 到点先删 1 个，随后每个 REPEAT 再删 1 个
        return;
    }
    if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if (backspaceLongPress) deleteLastChar();
        return;
    }
    if (code != LV_EVENT_CLICKED) return;
    if (backspaceLongPress) { backspaceLongPress = false; return; }
    deleteLastChar();
}
void keyMatrixEvent(lv_event_t *event) {
    // K2 双保险：CLICK_TRIG 已让 VALUE_CHANGED 在抬手派发，这里再挡“刚滑过”的抬手。
    if (Touch::swiped()) return;
    if(lv_event_get_code(event)!=LV_EVENT_VALUE_CHANGED)return;
    auto *matrix=(lv_obj_t*)lv_event_get_target(event);
    if(!matrix)return;
    // Button matrix passes the pressed button id through the event parameter;
    // lv_btnmatrix_get_selected_btn() is NONE again once the press is released.
    auto *param=(uint32_t*)lv_event_get_param(event);
    uint32_t id=param?*param:lv_btnmatrix_get_selected_btn(matrix);
    const char *text=(id==LV_BTNMATRIX_BTN_NONE)?nullptr:lv_btnmatrix_get_btn_text(matrix,(uint16_t)id);
    if(!text||!text[0])return;
    if(!activeInput)activeInput=passwordInput;
    if(!activeInput)return;
    lv_textarea_add_text(activeInput,text);
    lv_textarea_set_cursor_pos(activeInput,LV_TEXTAREA_CURSOR_LAST);
}
void selectEvent(lv_event_t *event) {
    size_t encoded=(size_t)(uintptr_t)lv_event_get_user_data(event);
    if(!encoded)return;
    size_t index=encoded-1;
    ssid=index<networks.size()?networks[index].ssid:"";password="";error="";editing=true;redraw();
}
void scanEvent(lv_event_t *) { editing=false;requestScan(); }
void returnEvent(lv_event_t *) { editing=false;password="";redraw(); }
void connectEvent(lv_event_t *) {
    if(connecting)return;
    ssid=lv_textarea_get_text(ssidInput);password=lv_textarea_get_text(passwordInput);
    bool secure=false;for(const auto &entry:networks)if(entry.ssid==ssid){secure=entry.secure;break;}
    if(ssid.isEmpty() || ssid.length()>32){error="网络名称为 1～32 字节";feedback();return;}
    if((secure || !password.isEmpty()) && (password.length()<8 || password.length()>63)){error="密码为 8～63 字节";feedback();return;}
    if(!save_ || !save_(ssid,password)){error="网络保存失败，请重试";feedback();return;}
    // The desktop loop starts WiFi.begin after the LVGL callback has returned.
    password="";lv_textarea_set_text(passwordInput,"");connecting=true;started=millis();error="";
    feedback();
}
lv_obj_t *localButton(lv_obj_t *parent,const String &text,int y,lv_event_cb_t callback,void *data=nullptr){
    auto *button=lv_btn_create(parent);lv_obj_set_pos(button,8,y);lv_obj_set_size(button,288,31);
    lv_obj_set_style_bg_color(button,lv_color_hex(Ui::Green),0);lv_obj_set_style_radius(button,5,0);
    auto *label=lv_label_create(button);lv_label_set_text(label,text.c_str());lv_obj_set_style_text_font(label,&desk_font,0);lv_obj_set_style_text_color(label,lv_color_hex(Ui::Ink),0);
    lv_obj_set_width(label,268);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_center(label);
    lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,data);return button;
}
// Compact button used by the manual-entry command row above the matrix keyboard.
lv_obj_t *editButton(lv_obj_t *parent,const String &text,int x,int y,int width,int height,lv_event_cb_t callback,void *data=nullptr){
    auto *button=lv_btn_create(parent);lv_obj_set_pos(button,x,y);lv_obj_set_size(button,width,height);
    lv_obj_set_style_bg_color(button,lv_color_hex(Ui::Green),0);lv_obj_set_style_radius(button,5,0);
    auto *label=lv_label_create(button);lv_label_set_text(label,text.c_str());lv_obj_set_style_text_font(label,&desk_font,0);lv_obj_set_style_text_color(label,lv_color_hex(Ui::Ink),0);
    lv_obj_set_width(label,width-8);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_obj_center(label);
    lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,data);return button;
}
// ---- access point recovery -------------------------------------------------
bool accessPointValid() {
    // INADDR_NONE would read 255.255.255.255; either state means "no usable AP".
    const uint32_t ip = (uint32_t)WiFi.softAPIP();
    return (WiFi.getMode() & WIFI_MODE_AP) != 0 && ip != 0 && ip != 0xFFFFFFFFu;
}
void logAccessPoint(const char *tag) {
    const uint32_t ip = (uint32_t)WiFi.softAPIP();
    Serial.printf("WIFI AP %s valid=%d ip=%u.%u.%u.%u mode=%d\n", tag, accessPointValid() ? 1 : 0,
                  (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF),
                  (int)WiFi.getMode());
}
// Deferred restore after the scan worker finished. Driven from poll()/loop so the
// blocking softAP/mode calls never run in the worker task.
void startAccessPointRestore() {
    if (accessPoint.isEmpty()) return;
    restoreStage = 1; restoreAt = millis(); restoreNextAt = restoreAt; restoreAttempts = 0;
    WiFi.mode(WIFI_OFF);
}
void pollAccessPointRestore() {
    if (!restoreStage) return;
    const uint32_t now = millis();
    if (restoreStage == 1) {
        if ((int32_t)(now - restoreAt) < 80) return;
        WiFi.mode(WIFI_AP_STA);
        restoreStage = 2; restoreAt = now;
        return;
    }
    if (restoreStage == 2) {
        if ((int32_t)(now - restoreAt) < 60) return;
        restoreStage = 3; restoreNextAt = now;
    }
    if ((int32_t)(now - restoreNextAt) < 0) return;
    const bool restored = WiFi.softAP(accessPoint.c_str(), accessPassword.c_str());
    if (restored || accessPointValid()) {
        logAccessPoint("restore");
        restoreStage = 0; restoreAttempts = 0; apFailures = 0; apRetryAt = 0;
        resumePending.store(true);
        if (active) redraw();
        return;
    }
    if (++restoreAttempts < 3) { restoreNextAt = now + 250; return; }
    // Give up on this pass; the desk loop watchdog rebuilds with exponential
    // backoff, and the station resume is still attempted so music/weather work.
    ++apRestoreFailures;
    logAccessPoint("restore-failed");
    Serial.printf("WIFI AP restore failed after %u attempts\n", (unsigned)restoreAttempts);
    restoreStage = 0; restoreAttempts = 0; apFailures = 0; apRetryAt = 0;
    resumePending.store(true);
}
// 5 s tick: the access point must be in AP mode with a usable IP. One rebuild
// attempt per tick at most, exponential backoff on failure, never a delay().
void pollAccessPointWatchdog() {
    if (accessPoint.isEmpty() || scanning.load() || restoreStage || connecting) return;
    const uint32_t now = millis();
    if (apRetryAt && (int32_t)(now - apRetryAt) < 0) return;
    if ((int32_t)(now - apCheckedAt) < 5000) return;
    apCheckedAt = now;
    if (accessPointValid()) {
        apFailures = 0; apRetryAt = 0;
        if (apReportedDown) { apReportedDown = false; Serial.println("WIFI AP watchdog: recovered"); if (active) redraw(); }
        return;
    }
    apReportedDown = true;
    if ((WiFi.getMode() & WIFI_MODE_AP) == 0) WiFi.mode(WIFI_AP_STA);
    const bool restored = WiFi.softAP(accessPoint.c_str(), accessPassword.c_str());
    if (restored && accessPointValid()) {
        ++apRebuilds; apFailures = 0; apRetryAt = 0; apReportedDown = false;
        logAccessPoint("watchdog-rebuilt");
        if (active) redraw();
        return;
    }
    if (apFailures < 6) ++apFailures;
    const uint32_t waitMs = std::min<uint32_t>(60000u, 5000u << (apFailures ? apFailures - 1 : 0));
    apRetryAt = now + waitMs;
    logAccessPoint("watchdog-failed");
}
}
void configure(SaveNetwork save,RefreshView refresh,const String &apSsid,const String &apPassword,ResumeStation resume){save_=save;refresh_=refresh;accessPoint=apSsid;accessPassword=apPassword;resume_=resume;}
bool busy(){return scanning.load() || connecting || restoreStage != 0;}
// Narrower than busy(): true only while the radio is owned by a scan or the scan's
// AP restore. The deferred WiFi.begin (deskLoop reconnectDue) implements the
// "connecting" state itself, so it must not be blocked by that same flag.
bool radioBusy(){return scanning.load() || restoreStage != 0;}
// AP watchdog: called once per deskLoop iteration; does nothing unless the 5 s
// tick is due, so it cannot block the LVGL/UI loop.
void maintainAccessPoint(){
    if (apSuspended) return;  // music playback suspends the SoftAP for radio coexistence
    pollAccessPointWatchdog();
}
bool accessPointHealthy(){return restoreStage == 0 && accessPointValid();}
int accessPointRestoreFailures(){return (int)apRestoreFailures;}
uint32_t accessPointRebuilds(){return apRebuilds;}
void leave(){
    active=false;keyMatrix=passwordInput=ssidInput=statusLabel=modeButton=backspaceButton=activeInput=nullptr;keyMode=0;password="";editing=false;
    backspaceLongPress=false;modeLongPress=false;
    // 键盘页把长按重复周期临时改成 90ms，离开时恢复 LVGL 默认值，避免影响其它页面。
    applyKeyboardLongPressTiming(LV_INDEV_DEF_LONG_PRESS_REP_TIME);
}
// 键盘层级（nav-back 调用）：editing（键盘态）是最内层，back 只退到列表态；
// 列表态返回 false，交给 desk.cpp 的 App 返回处理。
bool canBack(){return active && editing;}
bool back(){
    if(!active || !editing) return false;
    editing=false;password="";redraw();
    return true;
}
void requestScan(){
    if(busy())return;
    // Only the blocking scan runs in the worker. Dropping the AP is a short config
    // sequence done here so no worker task ever reconfigures the WiFi driver.
    scanDroppedAp.store((WiFi.getMode() & WIFI_MODE_AP) != 0 && WiFi.status() != WL_CONNECTED && !accessPoint.isEmpty());
    if (scanDroppedAp.load()) {
        WiFi.disconnect(false, false);
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    networks.clear();error="";WiFi.scanDelete();scanDone.store(false);scanResult.store(WIFI_SCAN_FAILED);
    scanning.store(true);editing=false;
    if(xTaskCreate(scanWorker,"wifi-scan",4096,nullptr,1,nullptr)!=pdPASS){
        scanning.store(false);
        if(scanDroppedAp.exchange(false))startAccessPointRestore();
        error="无法启动 Wi-Fi 搜索";
    }
    redraw();
}
void render(lv_obj_t *parent){
    active=true;keyMatrix=passwordInput=ssidInput=statusLabel=modeButton=backspaceButton=activeInput=nullptr;
    backspaceLongPress=false;modeLongPress=false;
    // 键盘页 90ms 连删；列表页恢复正常 100ms（leave() 也会恢复）。
    applyKeyboardLongPressTiming(editing ? 90 : LV_INDEV_DEF_LONG_PRESS_REP_TIME);
    if(!editing){
        auto *list=Ui::panel(parent,8,38,304,174,Ui::Paper);lv_obj_add_flag(list,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(list,LV_DIR_VER);
        String heading=WiFi.status()==WL_CONNECTED?"已连接："+WiFi.SSID():scanning.load()?"正在搜索 2.4GHz 网络":networks.empty()?"选择 2.4GHz 网络":"找到 "+String(networks.size())+" 个网络";
        auto *title=Ui::label(list,heading,8,2,286,&desk_font,Ui::Muted);
        lv_label_set_long_mode(title,LV_LABEL_LONG_DOT);
        localButton(list,scanning.load()?"正在搜索…":"重新搜索 Wi-Fi",30,scanEvent);
        int y=69;
        // Keep manual entry above the results: with a dozen networks the old
        // placement at the end forced a long scroll before it could be tapped.
        localButton(list,"手动输入网络名称",y,selectEvent,(void*)(uintptr_t)(networks.size()+1));y+=36;
        for(size_t index=0;index<networks.size();++index){
            String caption=(networks[index].secure?"* ":"")+networks[index].ssid+"   "+String(networks[index].rssi)+"dBm";
            localButton(list,caption,y,selectEvent,(void*)(uintptr_t)(index+1));y+=34;
        }
        if(!error.isEmpty())Ui::label(parent,error,10,214,300,&fd_sans_bold_10,0xA94330);
        return;
    }
    // Manual entry: two compact rows, a command row and a 4x10 ASCII keyboard
    // matrix. Nothing overlaps the fields and every key is fully visible.
    Ui::label(parent,"名称",10,38,42);
    ssidInput=lv_textarea_create(parent);lv_obj_set_pos(ssidInput,52,34);lv_obj_set_size(ssidInput,258,26);lv_textarea_set_one_line(ssidInput,true);lv_textarea_set_text(ssidInput,ssid.c_str());lv_textarea_set_max_length(ssidInput,32);lv_obj_set_style_text_font(ssidInput,&desk_font,0);lv_obj_add_event_cb(ssidInput,editEvent,LV_EVENT_ALL,nullptr);
    // K2：记录主题默认边框，供失焦还原（聚焦边框 #7FA97F 在 setActiveInput 里加）。
    inputBorderColor=lv_obj_get_style_border_color(ssidInput,0);
    inputBorderWidth=lv_obj_get_style_border_width(ssidInput,0);
    inputBorderSide=lv_obj_get_style_border_side(ssidInput,0);
    Ui::label(parent,"密码",10,67,42);
    passwordInput=lv_textarea_create(parent);lv_obj_set_pos(passwordInput,52,63);lv_obj_set_size(passwordInput,258,26);lv_textarea_set_one_line(passwordInput,true);lv_textarea_set_password_mode(passwordInput,true);lv_textarea_set_max_length(passwordInput,63);lv_textarea_set_text(passwordInput,password.c_str());lv_obj_set_style_text_font(passwordInput,&desk_font,0);lv_obj_add_event_cb(passwordInput,editEvent,LV_EVENT_ALL,nullptr);
    activeInput=nullptr;setActiveInput(passwordInput);
    // K2：退格追加长按连删；模式按钮追加长按跳符号页。每按钮每种事件只注册一次。
    backspaceButton=editButton(parent,"退格",10,92,72,26,backspaceEvent);
    lv_obj_add_event_cb(backspaceButton,backspaceEvent,LV_EVENT_LONG_PRESSED,nullptr);
    lv_obj_add_event_cb(backspaceButton,backspaceEvent,LV_EVENT_LONG_PRESSED_REPEAT,nullptr);
    lv_obj_add_event_cb(backspaceButton,backspaceEvent,LV_EVENT_PRESS_LOST,nullptr);
    modeButton=editButton(parent,"小写",88,92,72,26,modeEvent);
    lv_obj_add_event_cb(modeButton,modeEvent,LV_EVENT_LONG_PRESSED,nullptr);
    lv_obj_add_event_cb(modeButton,modeEvent,LV_EVENT_PRESS_LOST,nullptr);
    editButton(parent,"保存",166,92,72,26,connectEvent);
    editButton(parent,"返回",244,92,72,26,returnEvent);
    statusLabel=Ui::label(parent,"",10,118,300,&fd_sans_bold_10,0xA94330);feedback();
    keyMatrix=lv_btnmatrix_create(parent);
    lv_obj_set_pos(keyMatrix,0,134);lv_obj_set_size(keyMatrix,320,106);
    lv_btnmatrix_set_map(keyMatrix,(const char**)keyMapForMode());
    lv_obj_set_style_bg_opa(keyMatrix,LV_OPA_TRANSP,0);
    lv_obj_set_style_pad_all(keyMatrix,2,0);
    lv_obj_set_style_pad_row(keyMatrix,2,0);
    lv_obj_set_style_pad_column(keyMatrix,2,0);
    lv_obj_set_style_bg_color(keyMatrix,lv_color_hex(0xFFFFFF),LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyMatrix,LV_OPA_COVER,LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyMatrix,&lv_font_montserrat_14,LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyMatrix,lv_color_hex(Ui::Ink),LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyMatrix,1,LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyMatrix,lv_color_hex(0xD8E3CC),LV_PART_ITEMS);
    lv_obj_set_style_radius(keyMatrix,4,LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyMatrix,lv_color_hex(0xE6EFDB),LV_PART_ITEMS|LV_STATE_PRESSED);
    lv_obj_set_style_border_color(keyMatrix,lv_color_hex(0x9DBF9A),LV_PART_ITEMS|LV_STATE_PRESSED);  // K2：按下边框
    lv_obj_add_event_cb(keyMatrix,keyMatrixEvent,LV_EVENT_VALUE_CHANGED,nullptr);
    // K2：全键抬手触发 + 符号页 "SPACE" 3 单位宽。必须放在所有样式之后：
    // set_ctrl_map 用当前 pad 重算键区（样式变更也会触发 set_map），否则键宽会算错。
    applyKeyMap();
}
void poll(){
    if(scanning.load()&&scanDone.load()){scanning.store(false);scanDone.store(false);int result=scanResult.load();
        if(result<0)error="搜索失败，请重试";
        else{
            networks.clear();
            for(const auto &entry:pendingNetworks){
                bool duplicate=false;for(const auto &n:networks)if(n.ssid==entry.ssid)duplicate=true;
                if(!duplicate)networks.push_back(entry);
            }
            pendingNetworks.clear();
            std::sort(networks.begin(),networks.end(),[](const Network &a,const Network &b){return a.rssi>b.rssi;});
            if(networks.empty())error="未找到 2.4GHz 网络";
        }
        if(!editing)redraw();
    }
    // WiFi configuration from the scan path is completed on this thread only.
    if(!scanning.load() && scanDroppedAp.exchange(false))startAccessPointRestore();
    pollAccessPointRestore();
    if(resumePending.exchange(false)&&resume_)resume_();
    auto current=WiFi.status();
    if(connecting && millis()-started>1000 && current==WL_CONNECTED && WiFi.SSID()==ssid){connecting=false;editing=false;error="";redraw();}
    else if(connecting && (current==WL_CONNECT_FAILED || millis()-started>20000)){connecting=false;error="连接失败：请确认热点为 2.4GHz、密码正确且在范围内";feedback();}
    if(current!=previous){previous=current;if(active&&!editing)redraw();else feedback();}
}
}
