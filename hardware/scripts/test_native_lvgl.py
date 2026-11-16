"""Compile the actual LVGL UI/data code for a host-only, offscreen test.

Hardware interfaces are substituted; UI widgets, fonts, layouts, validation,
calendar maths and the JSON file commit/reload code are the firmware's originals.
No serial port is opened and no device is flashed.
"""
from pathlib import Path
import concurrent.futures
import hashlib
import json
import subprocess
import os
import argparse
import zlib
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/host-lvgl'
SKETCH=ROOT/'hardware/arduino/yiyi_desk'
STUB=ROOT/'hardware/tests/host'
LVGL=Path.home()/'Documents/Arduino/libraries/lvgl'
JSON=Path.home()/'Documents/Arduino/libraries/ArduinoJson/src'
ZIG=ROOT/'tools/host-python/ziglang/zig.exe'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--games',action='store_true',help='Test the unified Games launcher, all ten entries, nested returns and list pagination')
    parser.add_argument('--clock-only',action='store_true',help='Run the focused clock rendering checks')
    parser.add_argument('--pages',action='store_true',help='Render the hand-written pages (home/weather/reading/daily) offscreen')
    parser.add_argument('--days',action='store_true',help='Test and render the countdown days app and phone navigation')
    parser.add_argument('--mini-clocks',action='store_true',help='Test Pixel Clock, split-flap motion, rollovers and page cleanup')
    parser.add_argument('--dashboard-clock',action='store_true',help='Test all Clock Dashboard layouts, animation, settings and storage')
    parser.add_argument('--focus',action='store_true',help='Test and render actual alarm/pomodoro UI, scheduling and storage')
    parser.add_argument('--tetris',action='store_true',help='Test and render the actual Tetris mini app (engine, buttons, gravity)')
    parser.add_argument('--snake',action='store_true',help='Test and render the actual touch Snake app with LVGL pointer input')
    parser.add_argument('--jump',action='store_true',help='Test and render the actual Jump app with pointer hold/release input')
    parser.add_argument('--sheep',action='store_true',help='Test and render actual Sheep card matching through LVGL pointer input')
    parser.add_argument('--miner',action='store_true',help='Test and render the actual Gold Miner app through LVGL pointer input')
    parser.add_argument('--plane',action='store_true',help='Test and render the actual Plane app through LVGL pointer input')
    parser.add_argument('--breakout',action='store_true',help='Test and render the actual Breakout app through LVGL pointer input')
    parser.add_argument('--mines',action='store_true',help='Test and render the actual Mines app through LVGL pointer input')
    parser.add_argument('--pin',action='store_true',help='Test and render the actual Pin app through LVGL pointer input')
    parser.add_argument('--memory',action='store_true',help='Test and render the actual Memory app through LVGL pointer input')
    parser.add_argument('--music',action='store_true',help='Test and render the independent music UI and 2x2 launcher')
    parser.add_argument('--out',help='Directory for --pages screenshots (default build/host-pages)')
    parser.add_argument('--obj',help='Object cache / runner directory (default build/host-lvgl)')
    parser.add_argument('--compiler',help='Path to clang++ (or zig); defaults to bundled Zig')
    args=parser.parse_args()
    global OUT
    if args.obj:OUT=Path(args.obj)
    compiler=Path(args.compiler).resolve() if args.compiler else ZIG
    if not compiler.exists():raise RuntimeError('Install bundled Zig or pass --compiler /path/to/clang++')
    zig=compiler.stem.lower()=='zig'
    def driver(cpp):
        if zig:return [str(compiler),'c++' if cpp else 'cc','-target','x86_64-windows-gnu']
        return [str(compiler if cpp else compiler.with_name(compiler.name.replace('clang++','clang').replace('g++.exe','gcc.exe')))]
    os.environ['PATH']=str(compiler.parent)+os.pathsep+os.environ.get('PATH','')
    # The bundled Zig defaults to %LOCALAPPDATA%\zig, which this machine's policy
    # can deny (AccessDenied on the first cache write); keep its caches in the
    # workspace like the Arduino build does with esptool.
    os.environ.setdefault('ZIG_GLOBAL_CACHE_DIR',str(ROOT/'build/zig-cache/global'))
    os.environ.setdefault('ZIG_LOCAL_CACHE_DIR',str(ROOT/'build/zig-cache/local'))
    OUT.mkdir(parents=True,exist_ok=True)
    # Isolate each run's writable store; retain earlier runs for review.
    store=OUT/('store-'+str(os.getpid()));store.mkdir(exist_ok=True)
    # The runner's stable store contains only this host test's data.
    for filename in ['gift.json','gift.bak','gift.tmp']:
        path=OUT/'store'/filename
        if path.is_file():path.rename(store/filename)
    include=[STUB,SKETCH,LVGL,JSON,ROOT]
    c=list((LVGL/'src').rglob('*.c'))+[SKETCH/'desk_font.c']+list((SKETCH/'src/gift_assets').glob('*.c'))+list((SKETCH/'src/saved_design').rglob('*.c'))+list((SKETCH/'src/pages/assets').rglob('*.c'))
    runner=STUB/('games_launcher_runner.cpp' if args.games else 'days_runner.cpp' if args.days else 'dashboard_clock_runner.cpp' if args.dashboard_clock else 'breakout_runner.cpp' if args.breakout else 'mines_runner.cpp' if args.mines else 'pin_runner.cpp' if args.pin else 'memory_runner.cpp' if args.memory else 'miner_runner.cpp' if args.miner else 'plane_runner.cpp' if args.plane else 'sheep_runner.cpp' if args.sheep else 'jump_runner.cpp' if args.jump else 'snake_runner.cpp' if args.snake else 'music_runner.cpp' if args.music else 'focus_apps_runner.cpp' if args.focus else 'tetris_runner.cpp' if args.tetris else 'mini_clocks_runner.cpp' if args.mini_clocks else 'pages_runner.cpp' if args.pages else 'saved_design_runner.cpp')
    pages_cpp=sorted((SKETCH/'src/pages').glob('*.cpp'))
    cpp=[SKETCH/'src/app_state.cpp',SKETCH/'src/ui_styles.cpp',SKETCH/'src/ui_widgets.cpp',SKETCH/'src/gift_data.cpp',SKETCH/'src/power_status.cpp',SKETCH/'src/gift_ui.cpp',SKETCH/'src/gift_lunar.cpp',SKETCH/'src/schedule_data.cpp',SKETCH/'src/schedule_store.cpp',*list((SKETCH/'src/saved_design').rglob('*.cpp')),*pages_cpp,STUB/'support.cpp',runner]
    cpp.append(SKETCH/'src/music_player.cpp')
    cpp.append(SKETCH/'src/radio_player.cpp')
    cpp.append(SKETCH/'src/radio_catalog.cpp')
    def compile_file(source):
        identity=hashlib.sha1((str(source)+str(compiler)).encode()).hexdigest()[:10];obj=OUT/(source.stem+'-'+identity+'.o')
        cmd=driver(source.suffix=='.cpp')+['-O1','-UNDEBUG','-DLV_CONF_INCLUDE_SIMPLE','-DLV_LVGL_H_INCLUDE_SIMPLE','-DGIFT_HOST_TEST','-std=c++17' if source.suffix=='.cpp' else '-std=c99']
        if source.suffix=='.cpp':cmd+=['-include',str(STUB/'posix_time.h')]
        # LVGL 8.4's optional Pinyin IME passes its private struct to an
        # lv_obj_t API. Keep the vendor diagnostic visible, as GCC does.
        if not zig and source.name=='lv_ime_pinyin.c':cmd+=['-Wno-error=incompatible-pointer-types']
        for path in include:cmd+=['-I',str(path)]
        # Header changes affect all C++ UI tests. LVGL is rebuilt when its config changes.
        newest=max(source.stat().st_mtime,(SKETCH/'lv_conf.h').stat().st_mtime,Path(__file__).stat().st_mtime)
        if source.suffix=='.cpp':newest=max(newest,*[p.stat().st_mtime for p in STUB.glob('*.h')],*[p.stat().st_mtime for p in (SKETCH/'src').rglob('*.h')])
        if not obj.exists() or obj.stat().st_mtime<newest:
            result=subprocess.run(cmd+['-c',str(source),'-o',str(obj)],capture_output=True,text=True,encoding='utf-8',errors='replace')
            if result.returncode:raise RuntimeError(str(source)+'\n'+result.stderr[-10000:])
        return obj
    # Fail quickly on C++ interface errors before compiling the LVGL library.
    objects=[compile_file(source) for source in cpp]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:objects+=list(pool.map(compile_file,c))
    executable=OUT/('lvgl-games.exe' if args.games else 'lvgl-days.exe' if args.days else 'lvgl-dashboard-clock.exe' if args.dashboard_clock else 'lvgl-breakout.exe' if args.breakout else 'lvgl-mines.exe' if args.mines else 'lvgl-pin.exe' if args.pin else 'lvgl-memory.exe' if args.memory else 'lvgl-miner.exe' if args.miner else 'lvgl-plane.exe' if args.plane else 'lvgl-sheep.exe' if args.sheep else 'lvgl-jump.exe' if args.jump else 'lvgl-snake.exe' if args.snake else 'lvgl-focus-apps.exe' if args.focus else 'lvgl-tetris.exe' if args.tetris else 'lvgl-mini-clocks.exe' if args.mini_clocks else 'lvgl-pages-runner.exe' if args.pages else 'lvgl-runner.exe')
    result=subprocess.run(driver(True)+[str(obj) for obj in objects]+['-o',str(executable)],capture_output=True,text=True,encoding='utf-8',errors='replace')
    if result.returncode:raise RuntimeError(result.stderr[-10000:])
    def convert(raw):
        b=raw.read_bytes();image=Image.new('RGB',(320,240));pixels=[]
        for offset in range(0,len(b),2):
            value=int.from_bytes(b[offset:offset+2],'little');pixels.append(((value>>11)*255//31,((value>>5)&63)*255//63,(value&31)*255//31))
        image.putdata(pixels);out=raw.with_suffix('.png');image.save(out);return out
    if args.games or args.days or args.dashboard_clock or args.pages or args.mini_clocks or args.focus or args.tetris or args.music or args.snake or args.jump or args.sheep or args.miner or args.plane or args.breakout or args.mines or args.pin or args.memory:
        pageOut=Path(args.out) if args.out else ROOT/'build/host-pages'
        pageOut.mkdir(parents=True,exist_ok=True)
        for stale in pageOut.glob('*.rgb565'):stale.unlink()
        subprocess.run([str(executable),str(pageOut)],cwd=ROOT,check=True)
        if args.mini_clocks:
            # Independently check every decoded frame and delay against Pillow,
            # including optimized GIF delta rectangles and palette changes.
            expected=[]
            gif_dir=SKETCH/'src/pages/assets/clock_gifs'
            manifest=json.loads((gif_dir/'manifest.json').read_text(encoding='utf-8'))
            for clip,item in enumerate(manifest):
                gif=Image.open(gif_dir/(item['symbol']+'.gif'))
                for frame in range(gif.n_frames):
                    gif.seek(frame);rgb=gif.convert('RGB').tobytes();packed=bytearray(len(rgb))
                    for at in range(0,len(rgb),3):
                        value=((rgb[at]>>3)<<11)|((rgb[at+1]>>2)<<5)|(rgb[at+2]>>3)
                        packed[at]=value&255;packed[at+1]=value>>8;packed[at+2]=255
                    expected.append((clip,frame,gif.info['duration'],zlib.crc32(packed)))
            actual=[tuple(map(int,line.split())) for line in (pageOut/'gif-decoder-crc32.txt').read_text().splitlines()]
            assert actual==expected, next(((a,b) for a,b in zip(actual,expected) if a!=b),(len(actual),len(expected)))
            print(f'PASS: all {len(expected)} GIF frames and delays match Pillow in RGB565/alpha')
        shots={raw.stem:convert(raw) for raw in sorted(pageOut.glob('*.rgb565'))}
        order=(['clock-picker','pixel-clock','pixel-clock-snake','pixel-clock-no-time','flip-before','flip-folding','flip-hinge','flip-unfolding','flip-after','gif-clock-0-still','gif-clock-1-still','gif-clock-2-still','gif-clock-0-playing','gif-clock-2-playing'] if args.mini_clocks else ['home-online','home-offline','weather-online','weather-offline-cache','weather-offline-empty','weather-updating','reading-main','reading-quote-detail','reading-poem-detail','daily-main','daily-mood-editor','daily-water-detail','daily-wood-detail','schedule-current','apps','settings'])
        if args.games:
            order=['launcher-1','launcher-2','launcher-3','games-1','games-2','games-return']+['open-'+str(i) for i in range(1,11)]
        if args.dashboard_clock:
            order=['dashboard-picker','dashboard-hhmm','dashboard-hhmmss','minimal-hhmm','minimal-hhmmss','compact-hhmm','settings-colors','settings-fonts','settings-time','settings-weather','settings-custom-color']
            order=[name for name in order if name in shots]+sorted(name for name in shots if name not in order and not name.startswith('motion-'))
        if args.focus:
            order=['launcher-1','launcher-2','alarm-01-list','alarm-02-time','alarm-03-settings','alarm-04-sounds','alarm-05-ringing','alarm-06-repeat','alarm-07-more','alarm-08-delete','alarm-00-empty','pomo-01-ready','pomo-02-running','pomo-03-paused','pomo-04-focus-complete','pomo-05-resting','pomo-06-durations','pomo-07-reminders','pomo-08-complete','pomo-09-end-confirm','pomo-10-rest-paused','pomo-11-presets','pomo-12-interrupted','pomo-13-muted']
            order+=['alarm-direct-delete','timer-01-home','timer-02-duration','timer-03-add-preset','timer-04-sounds','timer-05-running','timer-06-paused','timer-06-background-finished','timer-07-finished','timer-08-reset-confirm','timer-09-muted']
        if args.music:
            order=['launcher-2','music-player','music-playlist','music-lyrics','music-volume']
        if args.tetris:
            order=['launcher-1','launcher-2','games-1','tetris-start','tetris-drop','tetris-paused-manual','tetris-play','tetris-paused','tetris-gameover']
        if args.snake:
            order=['launcher-2','games-1','snake-ready','snake-start','snake-play','snake-eat','snake-paused','snake-gameover','snake-won']
        if args.jump:
            order=['launcher-2','games-1','jump-ready','jump-start','jump-charging','jump-jumping','jump-center','jump-landed','jump-paused','jump-falling','jump-gameover']
        if args.sheep:
            order=['launcher-2','games-1','sheep-ready','sheep-start','sheep-tray','sheep-triple','sheep-paused','sheep-levelcleared','sheep-level2','sheep-gameover','sheep-won']
        if args.miner:
            order=['launcher-2','games-1','miner-ready','miner-start','miner-casting','miner-retracting','miner-score','miner-paused','miner-levelcleared','miner-level2','miner-gameover']
        if args.plane:
            order=['launcher-2','games-1','plane-ready','plane-start','plane-drag','plane-score','plane-hit','plane-paused','plane-gameover']
        if args.breakout:
            order=['launcher-2','games-2','breakout-ready','breakout-start','breakout-drag','breakout-score','breakout-paused','breakout-play','breakout-gameover','breakout-levelcleared','breakout-level2','breakout-won']
        if args.mines:
            order=['launcher-2','games-2','mines-ready','mines-start','mines-flag-mode','mines-long-flag','mines-opening','mines-board','mines-paused','mines-gameover','mines-won']
        if args.pin:
            order=['launcher-2','games-2','pin-ready','pin-start','pin-flight','pin-score','pin-paused','pin-gameover','pin-levelcleared','pin-level2','pin-level3','pin-won']
        if args.memory:
            order=['launcher-2','games-2','memory-ready','memory-start','memory-flip','memory-mismatch','memory-pair','memory-score','memory-paused','memory-levelcleared','memory-level2','memory-level3','memory-won']
            order=[name for name in order if name in shots]+sorted(name for name in shots if name.startswith('memory-') and name not in order)
        if args.days:
            order=sorted(shots)
        sheet=Image.new('RGB',(320*4,240*((len(order)+3)//4)),(230,238,224))
        for i,name in enumerate(order):
            image=shots.get(name)
            if image is not None:sheet.paste(Image.open(image),((i%4)*320,(i//4)*240))
        sheet.save(pageOut/'pages-contact.png')
        print('Host page screenshots:',pageOut)
        print('Contact sheet:',pageOut/'pages-contact.png')
        return
    subprocess.run([str(executable)]+(['--clock-only'] if args.clock_only else []),cwd=ROOT,check=True)
    for raw in OUT.glob('*.rgb565'):convert(raw)
    if args.clock_only:
        thumbs=Image.new('RGB',(320*3,240*2))
        for i,period in enumerate(['predawn','morning','day','sunset','dusk','night']):
            thumbs.paste(Image.open(OUT/f'clock-landscape-{period}.png'),((i%3)*320,(i//3)*240))
        thumbs.save(OUT/'clock-periods-contact.png')
        print('Native LVGL clock screenshots:',OUT/'clock-periods-contact.png')
        return
    thumbs=Image.new('RGB',(320*4,240*2),(230,238,224))
    for i in range(7):thumbs.paste(Image.open(OUT/f'page-{i}.png'),((i%4)*320,(i//4)*240))
    thumbs.paste(Image.open(OUT/'settings.png'),(960,240));thumbs.save(OUT/'seven-pages-contact.png')
    print('Native LVGL screenshots:',OUT/'seven-pages-contact.png')

if __name__=='__main__':main()
