#!/usr/bin/env bash
# The shaders the console compiled itself since the shader pack - the runtime keeps them in
# /data/SSBU/dump/shadercache-new (runtime/gpu_d3d11.c new_dir, v00.18 on) and names each in ssbu.log
# ("not in the shader cache, compiled from HLSL") - fetched for the next pack (scripts/pack-shadercache.py,
# scripts/build-shaderpack.sh). Never list /data/SSBU/dump/shadercache itself over FTP: 45,000 files, and the
# console's FTP server stopped answering for minutes after that listing (2026-10-21).
#   scripts/fetch-new-shaders.sh [out dir]      (default ~/ssbu-build/shaderpack/console-new)
set -euo pipefail
HOST="ftp://${PS4_HOST:-192.168.0.90}:${PS4_FTP_PORT:-2121}"
OUT="${1:-$HOME/ssbu-build/shaderpack/console-new}"
mkdir -p "$OUT"
names="$(curl -sS --max-time 60 --list-only "$HOST/data/SSBU/dump/shadercache-new/" | tr -d '\r' | grep -E '^v[0-9]+_(vs|ps|cs)_[0-9a-f_]+\.dxbc$' || true)"
fetched=0
for name in $names; do
    [ -s "$OUT/$name" ] && continue
    curl -sS --max-time 60 -o "$OUT/$name" "$HOST/data/SSBU/dump/shadercache-new/$name" && fetched=$((fetched + 1))
done
echo "$(printf '%s\n' "$names" | grep -c . || true) on the console, $fetched fetched into $OUT"
