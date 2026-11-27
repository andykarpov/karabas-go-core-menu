#!/bin/sh
# the boot intro (intro.c) on the host: its frames at 60 Hz and its MIDI, a video with the sound of
# a General MIDI soundfont (fluidsynth: an idea of the sound only, the SAM2695 sounds its own way)
#   host/intro.sh [640|800] [board 1|2|3] -> host/out/intro<w>/intro.mp4
set -e
cd "$(dirname "$0")/.."
m=${1:-640}; board=${2:-3}
w=$m; [ "$m" = 800 ] && w=800
d=host/out/intro$w
mkdir -p "$d"
rm -f "$d"/f*.ppm
gcc -O1 -g -DHOST -DGFX_DEBUG_PUTS=host_puts -Wall -Wno-unused-function -I. -o host/out/menu_host \
  -Ilib host/host.c intro.c ui.c settings.c browser.c groups.c setup.c gfx.c fonts.c sound.c esp.c wifi.c wifi_page.c pad_page.c esp_update.c ntp.c lib/esp_at_parse.c lib/time_zone.c lib/masked_input.c
HOST_BOARD=$board ./host/out/menu_host "$m" intro | grep -v '^frame f'
sf=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
fluidsynth -ni -q -F "$d/intro.wav" -r 48000 "$sf" "$d/intro.mid" >/dev/null 2>&1
ffmpeg -loglevel error -y -framerate 60 -i "$d/f%04d.ppm" -i "$d/intro.wav" \
  -c:v libx264 -pix_fmt yuv420p -crf 16 -c:a aac -b:a 192k -shortest "$d/intro.mp4"
python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$d/menu.ppm" "$d/menu.png"
echo "$d/intro.mp4"
