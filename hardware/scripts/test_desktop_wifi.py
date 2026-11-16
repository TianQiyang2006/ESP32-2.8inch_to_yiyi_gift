"""Compile unchanged production WiFi board/bridge sources against host interfaces.

Timers, network events and NVS are controlled fixtures. This exercises behavior;
it does not claim to test the radio, cloud service, or physical voice quality.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/host-desktop-wifi'
TEST = ROOT / 'hardware/tests/desktop_wifi'
BOARD = ROOT / 'hardware/esp-idf/yiyi_xiaozhi/main/boards'
ZIG = ROOT / 'tools/host-python/ziglang/zig.exe'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for source in [BOARD / 'common/wifi_board.cc', BOARD / 'common/wifi_board.h',
                   BOARD / 'yiyi-es3c28p/desktop_bridge.cc', BOARD / 'yiyi-es3c28p/desktop_bridge.h',
                   TEST / 'support.h', TEST / 'runner.cpp']:
        shutil.copyfile(source, OUT / source.name)
    headers = ['board.h', 'display.h', 'application.h', 'system_info.h', 'settings.h',
               'assets/lang_config.h', 'freertos/FreeRTOS.h', 'freertos/event_groups.h',
               'freertos/task.h', 'esp_timer.h', 'esp_network.h', 'esp_log.h',
               'material_symbols.h', 'esp_mac.h', 'wifi_manager.h', 'wifi_station.h', 'ssid_manager.h',
               'afsk_demod.h', 'nvs.h', 'esp_ota_ops.h', 'esp_system.h']
    for name in headers:
        stub = OUT / name
        stub.parent.mkdir(parents=True, exist_ok=True)
        stub.write_text('#pragma once\n', encoding='utf-8')
    exe = OUT / 'desktop-wifi-test.exe'
    subprocess.run([str(ZIG), 'c++', '-std=c++17', '-target', 'x86_64-windows-gnu',
                    '-I', str(OUT), '-include', str(OUT / 'support.h'),
                    str(OUT / 'wifi_board.cc'), str(OUT / 'desktop_bridge.cc'),
                    str(OUT / 'runner.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

    wake = OUT / 'wake'
    wake.mkdir(exist_ok=True)
    source_dir = ROOT / 'hardware/esp-idf/yiyi_xiaozhi/main/audio/wake_words'
    for source in [source_dir / 'wake_word_audio_cache.cc', source_dir / 'wake_word_audio_cache.h',
                   TEST / 'cache_support.h', TEST / 'cache_runner.cpp']:
        shutil.copyfile(source, wake / source.name)
    for name in ['esp_heap_caps.h', 'esp_log.h']:
        stub = wake / name
        stub.parent.mkdir(parents=True, exist_ok=True)
        stub.write_text('#pragma once\n', encoding='utf-8')
    exe = wake / 'wake-test.exe'
    subprocess.run([str(ZIG), 'c++', '-std=c++17', '-target', 'x86_64-windows-gnu',
                    '-I', str(wake), '-include', str(wake / 'cache_support.h'),
                    str(wake / 'wake_word_audio_cache.cc'), str(wake / 'cache_runner.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    launch = OUT / 'launcher'
    launch.mkdir(exist_ok=True)
    for source in [ROOT / 'hardware/arduino/yiyi_desk/src' / name for name in
                   ['xiaozhi_voice.cpp', 'xiaozhi_voice.h', 'gift_data.h']]:
        shutil.copyfile(source, launch / source.name)
    for name in ['launcher_support.h', 'launcher_runner.cpp']:
        shutil.copyfile(TEST / name, launch / name)
    for name in ['WiFi.h', 'Preferences.h', 'esp_ota_ops.h']:
        (launch / name).write_text('#pragma once\n', encoding='utf-8')
    exe = launch / 'launcher-test.exe'
    subprocess.run([str(ZIG), 'c++', '-std=c++17', '-target', 'x86_64-windows-gnu',
                    '-I', str(launch), '-I', str(ROOT / 'hardware/tests/host'),
                    '-I', str(Path.home() / 'Documents/Arduino/libraries/ArduinoJson/src'),
                    '-include', str(launch / 'launcher_support.h'),
                    str(launch / 'xiaozhi_voice.cpp'), str(launch / 'launcher_runner.cpp'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
