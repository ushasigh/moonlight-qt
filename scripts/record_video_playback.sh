#!/bin/bash
# Screen record a video while it plays
# Usage: ./record_video_playback.sh <input_video> <output_video>

INPUT="${1:-$HOME/Downloads/paris_cif_reference.mp4}"
OUTPUT="${2:-$HOME/Downloads/paris_screen_recorded.mp4}"

# Get video duration
DURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 "$INPUT")
echo "Video duration: ${DURATION}s"

# Get the main display ID for screen capture
# On macOS, device index 1 is typically the main screen
SCREEN_DEVICE="1"

echo "Starting playback and screen recording..."
echo "Input: $INPUT"
echo "Output: $OUTPUT"

# Start screen recording in background
# Capture for duration + 1 second buffer
RECORD_DURATION=$(echo "$DURATION + 1" | bc)

ffmpeg -y -f avfoundation -framerate 30 -i "${SCREEN_DEVICE}:none" \
    -t "$RECORD_DURATION" \
    -c:v libx264 -preset ultrafast -crf 18 \
    -pix_fmt yuv420p \
    "$OUTPUT" &
RECORD_PID=$!

# Small delay to ensure recording starts
sleep 0.5

# Play the video (opens in default player)
open -W "$INPUT"

# Wait for recording to finish
wait $RECORD_PID

echo "Done! Screen recording saved to: $OUTPUT"
