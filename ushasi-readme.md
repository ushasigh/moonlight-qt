```bash
which brew
brew install qt@5
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH" && cd /Users/ushasighosh/Desktop/moonlight-qt/build && qmake ..
cd /Users/ushasighosh/Desktop/moonlight-qt && git submodule update --init --recursive
export PATH="/opt/homebrew/opt/qt@5/bin:$PATH" && cd /Users/ushasighosh/Desktop/moonlight-qt/build && rm -rf * && qmake .. && make -j8
cd /Users/ushasighosh/Desktop/moonlight-qt/build && find . -name "*.app" -type d
open /Users/ushasighosh/Desktop/moonlight-qt/build/app/Moonlight.app

## Running moonlight with logging enabled
./build/app/Moonlight.app/Contents/MacOS/Moonlight 2>&1 | tee logs/overlay_log_run1.txt

## Parse the logs to json format
python3 scripts/metrics_to_json.py --input logs/overlay_log_run1.txt --output logs/metrics_run1.json
## Use --watch to watch
```