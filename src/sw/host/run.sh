#!/bin/sh
# the host build of the menu drawing (host.c): the frames of the screens -> host/out/*.png
set -e
cd "$(dirname "$0")/.."
mkdir -p host/out
gcc -O0 -g -rdynamic -DHOST -DGFX_DEBUG_PUTS=dbg_puts -Wall -Wno-unused-function -I. -o host/out/menu_host \
  -Ilib host/host.c ui.c settings.c browser.c setup.c gfx.c fonts.c sound.c esp.c wifi.c wifi_page.c esp_update.c ntp.c lib/esp_at_parse.c lib/time_zone.c lib/masked_input.c 2>&1
./host/out/menu_host "$@"
cd host/out && for f in *.ppm; do python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[1][:-4] + '.png')" "$f" && rm "$f"; done
