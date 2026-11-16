#pragma once
#include <Arduino.h>
#include <lvgl.h>
namespace WifiSetup {
using SaveNetwork = bool (*)(const String &ssid, const String &password);
using RefreshView = void (*)();
using ResumeStation = void (*)();
void configure(SaveNetwork save, RefreshView refresh, const String &apSsid, const String &apPassword, ResumeStation resume);
void render(lv_obj_t *parent);
void leave();
// 键盘层级（nav-back 使用）：editing（键盘态）是最内层，back 只退到列表态并返回 true；
// 列表态返回 false，交给 desk.cpp 的 App 返回处理。canBack() 是只读查询，语义与 back() 同源。
bool canBack();
bool back();
void poll();
void requestScan();
bool busy();
// True only while a scan/the scan's AP restore owns the radio (not "connecting":
// the deferred WiFi.begin in deskLoop is what performs that connection step).
bool radioBusy();
// Access-point watchdog: cheap to call every loop iteration (5 s internal tick),
// rebuilds the SoftAP with exponential backoff when scan/restore left it broken.
void maintainAccessPoint();
bool accessPointHealthy();
int accessPointRestoreFailures();
uint32_t accessPointRebuilds();
}
