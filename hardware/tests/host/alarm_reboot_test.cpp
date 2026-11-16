// Includes the real implementation: test its callbacks and poll(), not a model.
#include "hardware/arduino/yiyi_desk/src/pages/focus_page.cpp"
#include <cassert>
#include <iostream>
#include <string>

int64_t HostEpoch = 0;
extern int HostKnocks, HostNoticeCount;
extern bool HostNoticeError;
extern String HostNotice;

namespace {
using State = AlarmPersistence::State;
using Journal = AlarmPersistence::Journal;
using Result = AlarmPersistence::LoadResult;
int64_t at(int day, int hour, int minute, int second = 0) {
    return DeskLogic::beijingEpoch(2026, 11, day, hour, minute) + second;
}
State setting() { State state; state.enabled = true; state.minute = 450; state.days = 0x3e; return state; }
State disk() {
    Preferences preferences; assert(preferences.begin("focus"));
    Journal journal(preferences); State state;
    auto result = journal.load(state);
    assert(result == Result::Ready || result == Result::Recovered);
    return state;
}
void seed(const State &state) {
    Preferences preferences; assert(preferences.begin("focus"));
    Journal journal(preferences); State previous; journal.load(previous);
    assert(journal.save(state)); preferences.end();
}
void initUi() {
    lv_init();
    static lv_color_t pixels[320 * 40];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, nullptr, 320 * 40);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 240; display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); };
    lv_disp_drv_register(&display);
    root = lv_scr_act(); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    Ui::setRoot(root); Ui::setActionHandler([](lv_event_t *) {});
    FocusApps::buildAlarm(root);
}
void unit() {
    Preferences preferences; assert(preferences.begin("journal-unit"));
    Journal journal(preferences); State state;
    assert(journal.load(state) == Result::Empty);
    state = setting(); assert(journal.save(state));
    auto changed = state; changed.minute = 525;
    Preferences::failNextWrites = 1;
    assert(!journal.save(changed));
    Journal afterFailure(preferences); State recovered;
    assert(afterFailure.load(recovered) == Result::Ready && recovered.minute == 450);
    Preferences::partialNextWriteBytes = 12;
    assert(!afterFailure.save(changed));
    Journal afterTornWrite(preferences);
    assert(afterTornWrite.load(recovered) == Result::Recovered && recovered.minute == 450);
    assert(afterTornWrite.save(changed));
    Journal afterGoodWrite(preferences);
    assert(afterGoodWrite.load(recovered) == Result::Ready && recovered.minute == 525);
    uint8_t bytes[Journal::RecordSize];
    assert(preferences.getBytes("alarm1", bytes, sizeof(bytes)) == sizeof(bytes));
    bytes[8] ^= 0x80;
    assert(preferences.putBytes("alarm1", bytes, sizeof(bytes)) == sizeof(bytes));
    Journal afterCorruption(preferences);
    assert(afterCorruption.load(recovered) == Result::Recovered && recovered.minute == 450);
    uint8_t junk[Journal::RecordSize]{};
    assert(preferences.putBytes("alarm0", junk, sizeof(junk)) == sizeof(junk));
    Journal unusable(preferences); assert(unusable.load(recovered) == Result::Corrupt);
    auto invalid = state; invalid.minute = 1440; assert(!journal.save(invalid));
    invalid = state; invalid.days = 0; assert(!journal.save(invalid));
    invalid = state; invalid.lastFired = 1234; assert(!journal.save(invalid));
    std::cout << "PASS: whole-record commits, partial writes, CRC recovery and invalid records\n";

    state = setting();
    assert(AlarmRecovery::due(state, 0) == 0);
    assert(AlarmRecovery::next(state, 0) == 0);
    assert(AlarmRecovery::due(state, at(2, 7, 29, 59)) == 0);
    assert(AlarmRecovery::due(state, at(2, 7, 30)) == at(2, 7, 30));
    assert(AlarmRecovery::due(state, at(2, 7, 30, 59)) == at(2, 7, 30));
    assert(AlarmRecovery::due(state, at(2, 7, 31)) == at(2, 7, 30));
    assert(AlarmRecovery::due(state, at(2, 7, 31, 1)) == 0);
    assert(AlarmRecovery::due(state, at(1, 7, 30)) == 0); // Sunday
    assert(AlarmRecovery::next(state, at(1, 9, 0)) == at(2, 7, 30));
    state.lastFired = at(2, 7, 30);
    assert(AlarmRecovery::due(state, at(2, 7, 30, 20)) == 0);
    assert(AlarmRecovery::due(state, at(1, 7, 30)) == 0);
    assert(AlarmRecovery::next(state, at(2, 7, 30)) == at(3, 7, 30));
    state = setting(); state.minute = 0; state.days = 0x7f;
    assert(AlarmRecovery::due(state, at(3, 0, 0, 30)) == at(3, 0, 0));
    assert(AlarmRecovery::next(state, DeskLogic::beijingEpoch(2026, 12, 31, 23, 59)) ==
           DeskLogic::beijingEpoch(2027, 1, 1));
    std::cout << "PASS: Beijing time, clock not ready, week rules, midnight/year boundaries, catch-up and deduplication\n";
}
}

int main(int argc, char **argv) {
    assert(argc == 2); std::string mode = argv[1];
    if (mode == "unit") unit();
    else if (mode == "seed") seed(setting());
    else if (mode == "restore-and-fire") {
        FocusApps::begin();
        assert(FocusApps::alarmEnabled && FocusApps::alarmMinute == 450 && FocusApps::alarmDays == 0x3e);
        HostEpoch = 0; FocusApps::poll(); assert(HostKnocks == 0);
        HostEpoch = at(2, 7, 29, 59); FocusApps::poll(); assert(HostKnocks == 0);
        HostEpoch = at(2, 7, 30); FocusApps::poll(); assert(HostKnocks == 1);
        assert(disk().lastFired == at(2, 7, 30));
    } else if (mode == "deduplicate-after-reboot") {
        FocusApps::begin(); HostEpoch = at(2, 7, 30, 20); FocusApps::poll(); assert(HostKnocks == 0);
        HostEpoch = at(2, 7, 29, 40); FocusApps::poll(); assert(HostKnocks == 0);
        HostEpoch = at(3, 7, 30); FocusApps::poll(); assert(HostKnocks == 1);
        assert(disk().lastFired == at(3, 7, 30));
    } else if (mode == "save-disabled") {
        auto state = disk(); state.enabled = false; seed(state);
    } else if (mode == "restore-disabled") {
        FocusApps::begin(); assert(!FocusApps::alarmEnabled);
        HostEpoch = at(4, 7, 30); FocusApps::poll(); assert(HostKnocks == 0);
    } else if (mode == "seed-legacy") {
        Preferences preferences; assert(preferences.begin("focus"));
        assert(preferences.putBool("alarmOn", true) == 1);
        assert(preferences.putUShort("alarmMin", 495) == 2);
        assert(preferences.putUChar("alarmDays", 0x41) == 1);
        assert(preferences.putUChar("focusMin", 45) == 1);
    } else if (mode == "migrate-legacy") {
        FocusApps::begin();
        assert(FocusApps::alarmEnabled && FocusApps::alarmMinute == 495 && FocusApps::alarmDays == 0x41);
        assert(FocusApps::focusMinutes == 45);
        auto state = disk(); assert(state.enabled && state.minute == 495 && state.days == 0x41);
    } else if (mode == "seed-snooze") {
        auto state = setting(); state.lastFired = at(2, 7, 30); state.snoozeEpoch = at(2, 7, 40); seed(state);
    } else if (mode == "restore-snooze") {
        FocusApps::begin(); assert(FocusApps::snoozed && FocusApps::snoozeWaitingClock);
        HostEpoch = 0; FocusApps::poll(); assert(HostKnocks == 0);
        HostEpoch = at(2, 7, 39, 50); FocusApps::poll(); assert(HostKnocks == 0);
        HostMillis += 10000; HostEpoch = at(2, 7, 40); FocusApps::poll(); assert(HostKnocks == 1);
        assert(disk().snoozeEpoch == 0);
    } else if (mode == "snooze-no-duplicate") {
        FocusApps::begin(); HostEpoch = at(2, 7, 40, 20); FocusApps::poll(); assert(HostKnocks == 0);
    } else if (mode == "expired-snooze") {
        FocusApps::begin(); HostEpoch = at(2, 7, 42); FocusApps::poll();
        assert(HostKnocks == 0 && !FocusApps::snoozed && disk().snoozeEpoch == 0);
    } else if (mode == "save-failure-ui") {
        initUi(); assert(!FocusApps::alarmEnabled);
        Preferences::failNextWrites = 1;
        lv_event_send(FocusApps::alarmToggle, LV_EVENT_CLICKED, nullptr);
        assert(!FocusApps::alarmEnabled && !disk().enabled && FocusApps::alarmWriteError);
        assert(HostNoticeError && HostNotice.indexOf("保存失败") >= 0);
        lv_event_send(FocusApps::alarmToggle, LV_EVENT_CLICKED, nullptr);
        assert(FocusApps::alarmEnabled && disk().enabled && !FocusApps::alarmWriteError);
    } else if (mode == "storage-unavailable") {
        Preferences::failBegin = true; initUi();
        assert(!FocusApps::alarmEnabled && !FocusApps::alarmStorageReady);
        lv_event_send(FocusApps::alarmToggle, LV_EVENT_CLICKED, nullptr);
        assert(!FocusApps::alarmEnabled && HostNoticeError);
        assert(std::string(lv_label_get_text(FocusApps::alarmState)) == "存储不可用");
    } else if (mode == "late-startup") {
        FocusApps::begin(); HostEpoch = at(2, 7, 33); FocusApps::poll(); assert(HostKnocks == 0);
        assert(AlarmRecovery::next(FocusApps::alarmRecord, HostEpoch) == at(3, 7, 30));
    } else if (mode == "configure-ui") {
        initUi();
        for (int i = 0; i < 2; ++i) {
            for (uint32_t j = 0; j < lv_obj_get_child_cnt(root); ++j) {
                auto *child = lv_obj_get_child(root, j);
                if (lv_obj_get_child_cnt(child) != 1) continue;
                auto *caption = lv_obj_get_child(child, 0);
                if (lv_obj_check_type(caption, &lv_label_class) && std::string(lv_label_get_text(caption)) == "+5")
                    lv_event_send(child, LV_EVENT_CLICKED, nullptr);
            }
        }
        lv_event_send(FocusApps::dayButtons[0], LV_EVENT_CLICKED, nullptr);
        lv_event_send(FocusApps::dayButtons[6], LV_EVENT_CLICKED, nullptr);
        lv_event_send(FocusApps::alarmToggle, LV_EVENT_CLICKED, nullptr);
        auto state = disk(); assert(state.enabled && state.minute == 490 && state.days == 0x3e);
    } else if (mode == "restore-configured-ui") {
        FocusApps::begin();
        assert(FocusApps::alarmEnabled && FocusApps::alarmMinute == 490 && FocusApps::alarmDays == 0x3e);
        HostEpoch = at(2, 8, 10); FocusApps::poll(); assert(HostKnocks == 1);
    } else if (mode == "interrupt-write") {
        Preferences preferences; assert(preferences.begin("focus"));
        Journal journal(preferences); State state;
        assert(journal.load(state) == Result::Ready);
        state.minute = 525; Preferences::partialNextWriteBytes = 12;
        assert(!journal.save(state));
    } else if (mode == "recover-interrupted") {
        FocusApps::begin();
        assert(FocusApps::alarmStorageReady && FocusApps::alarmEnabled && FocusApps::alarmMinute == 450);
        HostEpoch = at(2, 7, 30); FocusApps::poll(); assert(HostKnocks == 1);
    } else if (mode == "corrupt-both") {
        Preferences preferences; assert(preferences.begin("focus"));
        uint8_t bad[Journal::RecordSize]{};
        assert(preferences.putBytes("alarm0", bad, sizeof(bad)) == sizeof(bad));
        assert(preferences.putBytes("alarm1", bad, sizeof(bad)) == sizeof(bad));
    } else if (mode == "restore-corrupt") {
        initUi(); HostEpoch = at(2, 8, 0); FocusApps::poll();
        assert(!FocusApps::alarmStorageReady && !FocusApps::alarmEnabled && HostKnocks == 0);
        assert(std::string(lv_label_get_text(FocusApps::alarmState)) == "存储不可用");
    } else if (mode == "configure-snooze-ui") {
        seed(setting()); initUi(); HostEpoch = at(2, 7, 30); FocusApps::poll(); assert(HostKnocks == 1);
        lv_event_send(FocusApps::snoozeButton, LV_EVENT_CLICKED, nullptr);
        assert(FocusApps::snoozed && !FocusApps::alarmRinging && disk().snoozeEpoch == at(2, 7, 40));
    } else if (mode == "waiting-clock-ui") {
        seed(setting()); initUi(); FocusApps::poll();
        assert(HostKnocks == 0 && std::string(lv_label_get_text(FocusApps::alarmState)) == "开启·待校时");
    } else { std::cerr << "Unknown test mode\n"; return 2; }
    std::cout << "PASS: " << mode << "\n";
    return 0;
}
