#!/bin/zsh
# Copies the HD renders (tools/blender with MH_RES=2) and the zones' far
# scenery from an AmoledOS checkout into art/, where CI packs them into
# monsterhop_hd.pak. Scratch (names starting with "_") stays behind.
#   tools/sync_art.sh [path to AmoledOS]
set -e
cd ${0:a:h}/..
AOS=${1:-../ESP32S3_AmoledOS}
SRC=$AOS/apps/monsterhop
[[ -d $SRC/assets_hd ]] || { echo "no $SRC/assets_hd: render with MH_RES=2 first"; exit 1; }
rsync -a --delete --exclude '_*' $SRC/assets_hd/ art/hd/
mkdir -p art/backdrops
rsync -a --delete --exclude 'backdrop_*_1x.png' --include 'backdrop_*.png' --include 'meta.json' --exclude '*' \
    $SRC/assets/backdrops/ art/backdrops/
du -sh art/hd art/backdrops
