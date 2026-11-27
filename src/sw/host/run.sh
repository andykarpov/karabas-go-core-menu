#!/bin/sh
# the host build of the menu drawing (host.c): the frames of the screens in both video modes
# -> host/out/640/*.png, host/out/800/*.png
set -e
cd "$(dirname "$0")/.."
mkdir -p host/out/640 host/out/800
gcc -O0 -g -rdynamic -DHOST -DGFX_DEBUG_PUTS=host_puts -Wall -Wno-unused-function -I. -o host/out/menu_host \
  -Ilib host/host.c intro.c fx.c show.c ui.c settings.c browser.c groups.c setup.c gfx.c fonts.c sound.c esp.c wifi.c wifi_page.c pad_page.c esp_update.c ntp.c lib/esp_at_parse.c lib/time_zone.c lib/masked_input.c 2>&1
for m in 640 800; do ./host/out/menu_host $m; done
for d in host/out/640 host/out/800; do
  (cd $d && for f in *.ppm; do python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[1][:-4] + '.png')" "$f" && rm "$f"; done)
done
