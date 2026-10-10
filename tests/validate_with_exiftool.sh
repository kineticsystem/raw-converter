#!/bin/bash -e

# Converts the raw files of each sample folder with dng-convert and checks
# every DNG with exiftool's validator, which reports TIFF structure problems
# and missing or malformed DNG tags.
#
# Usage: validate_with_exiftool.sh <dng-convert> <exiftool> <work folder> <folders, colon separated>

convert="$1"
exiftool="$2"
work="$3"
IFS=: read -r -a folders <<< "$4"

rm -rf "$work"
mkdir -p "$work"
converted=0
for folder in "${folders[@]}"; do
  [ -d "$folder" ] || continue
  out="$work/$(basename "$folder")"
  "$convert" --quiet --overwrite "$folder" "$out" || [ $? -eq 1 ]
  for dng in "$out"/*.dng; do
    [ -f "$dng" ] || continue
    converted=$((converted + 1))
    report=$("$exiftool" -validate -warning -error -a "$dng")
    if ! grep -q "Validate *: OK" <<< "$report"; then
      echo "$dng:" >&2
      echo "$report" >&2
      exit 1
    fi
  done
done
echo "$converted DNG files valid"
