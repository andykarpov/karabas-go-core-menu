#!/bin/sh
# the second boot animation (show.c) on the host, as intro.sh: its frames at 60 Hz piped to ffmpeg (every
# 30th also a .ppm), its MIDI with a General MIDI soundfont (fluidsynth: not the sound of the SAM2695)
#   host/show.sh [640|800] [show|count] -> host/out/show<w>/show.mp4
set -e
cd "$(dirname "$0")/.."
m=${1:-640}; mode=${2:-show}
w=$m
d=host/out/show$w
mkdir -p "$d"
rm -f "$d"/f*.ppm
gcc -O1 -g -DHOST -DGFX_DEBUG_PUTS=host_puts -Wall -Wno-unused-function -I. -o host/out/menu_host \
  -Ilib host/host.c intro.c fx.c show.c ui.c settings.c browser.c groups.c setup.c gfx.c fonts.c sound.c esp.c wifi.c wifi_page.c pad_page.c esp_update.c ntp.c lib/esp_at_parse.c lib/time_zone.c lib/masked_input.c
./host/out/menu_host "$m" "$mode" | grep -v '^frame f'
sf=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
fluidsynth -ni -q -F "$d/show.wav" -r 48000 "$sf" "$d/show.mid" >/dev/null 2>&1
ffmpeg -loglevel error -y -i "$d/video.mp4" -i "$d/show.wav" -c:v copy -c:a aac -b:a 192k -shortest "$d/show.mp4"
echo "$d/show.mp4"
