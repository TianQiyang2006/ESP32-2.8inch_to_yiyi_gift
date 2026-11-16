#ifndef _DESKTOP_BRIDGE_H_
#define _DESKTOP_BRIDGE_H_

// 双应用桥接：把桌面（app0）维护的 Wi-Fi / UUID 身份导入音乐小智（app1），
// 并提供退出回桌面的能力。桌面端对应 hardware/arduino/yiyi_desk/src/xiaozhi_voice.cpp。
void ImportDesktopIdentity();
void ExitToDesktop();

#endif // _DESKTOP_BRIDGE_H_
