#include "xiaozhi_voice.h"
#include "gift_data.h"
#include <cassert>
#include <iostream>

uint32_t HostMillis = 0;
int HostBatteryMv = 0;
JsonDocument deviceSettings;
namespace GiftData { JsonVariantConst get(const char* key) { return deviceSettings[key]; } }

int main() {
    using namespace XiaozhiVoice;
    deviceSettings["deviceSettings"]["volume"] = 53;
    deviceSettings["deviceSettings"]["brightness"] = 79;
    begin(); launch(); assert(busy()); poll();
    assert(get().stage == Stage::Error && get().message.find("桌面设置") != String::npos);
    assert(!busy() && bootChanges == 0 && restarts == 0 && preferenceWrites == 0);
    poll(); assert(bootChanges == 0);
    networkStatus = WL_CONNECTED;
    launch(); poll();
    assert(bootChanges == 1 && restarts == 1 && preferenceWrites == 3);
    assert(Preferences::saved["audio:output_volume"] == 53 && Preferences::saved["display:brightness"] == 79);
    assert(Preferences::saved["yiyi-desk:voiceReturn"] == 1);
    JsonDocument info; diagnostics(info);
    assert(info["implementation"].as<std::string>() == "official-2.5.0" && info["installed"].as<bool>());
    officialVersion = "2.1.0"; diagnostics(info);
    assert(info["implementation"].as<std::string>() == "official-2.1.0" && info["installed"].as<bool>());
    officialVersion = "1.9.0"; launch(); poll();
    assert(get().stage == Stage::Error && bootChanges == 1 && restarts == 1);
    officialVersion = "2.5.0"; officialProject = "other-app"; launch(); poll();
    assert(get().stage == Stage::Error && bootChanges == 1 && restarts == 1);
    officialProject = "xiaozhi";
    launch(); close(); poll(); assert(bootChanges == 1 && get().stage == Stage::Idle);
    officialInstalled = false; launch(); poll();
    assert(get().stage == Stage::Error && bootChanges == 1 && restarts == 1);
    officialInstalled = true; bootResult = -1; launch(); poll();
    assert(get().stage == Stage::Error && bootChanges == 2 && restarts == 1);
    std::cout << "PASS: offline launch blocked without reboot/writes, online launch, settings preservation, cancellation, missing image, boot failure\n";
}
