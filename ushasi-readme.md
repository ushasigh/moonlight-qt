## Starting Sunshine (wcsng-32)
[Documentation](https://github.com/ushasigh/Sunshine/blob/sync/docs/Video_recording.md)  

```bash
cd Sunshine_yoshi/
XDG_CONFIG_HOME=~/config-yoshi ./build-yoshi/sunshine
```
You can modify the settings in ``~/config-yoshi/sunshine/sunshine.conf``  

**Recorded Video session** stored in ``recorded-session/``  

## Starting Moonlight (wcsng 52)
[Documentation](https://github.com/ushasigh/moonlight-qt/blob/save_csv/docs/change_format.md)  

```bash
cd moonlight-qt/build/app
./moonlight >ushasi-run1.log 2>&1
```

You can modify the settings in ``~/.config/Moonlight Game Streaming Project/Moonlight.conf`` 

**Recorded Video Session** stored in ``moonlight-qt/recorded-session/``     

## VMAF comparison
[Documentation](https://github.com/ushasigh/Sunshine/blob/sync/docs/vmaf.md)  

```bash
python3 vmaf_compare.py \
 --reference sunshine_capture.yuv \
 --distorted moonlight_recording.csv \
 --ref-csv sunshine_capture.frames.csv \
 --dist-csv moonlight_recording.frames.csv \
 --meta sunshine_capture.yuv.meta \
 --log-file result.log \
 --log-per-frame \
 --model-path /path/to/vmaf_v0.6.1.json
```

## My MacOS build instructions
```bash
which brew
brew install qt@5
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH" && cd /Users/ushasighosh/Desktop/moonlight-qt/build && qmake ..
cd /Users/ushasighosh/Desktop/moonlight-qt && git submodule update --init --recursive
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH" && cd /Users/ushasighosh/Desktop/moonlight-qt/build && rm -rf * && qmake .. && make -j8
cd /Users/ushasighosh/Desktop/moonlight-qt/build && find . -name "*.app" -type d
open /Users/ushasighosh/Desktop/moonlight-qt/build/app/Moonlight.app

## Build
cd build
make -j$(sysctl -n hw.ncpu)

## Running moonlight with logging enabled
./build/app/Moonlight.app/Contents/MacOS/Moonlight 2>&1 | tee logs/overlay_log_run1.txt

## Parse the logs to json format
python3 scripts/metrics_to_json.py --input logs/overlay_log_run1.txt --output logs/metrics_run1.json
## Use --watch to watch
```

