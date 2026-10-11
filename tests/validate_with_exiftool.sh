#!/bin/bash -e

# Converts the raw files of each sample folder with raw-convert, to DNG and
# TIFF, and checks every file with exiftool's validator, which reports TIFF
# structure problems and missing or malformed DNG tags.
#
# Usage: validate_with_exiftool.sh <raw-convert> <exiftool> <work folder> <folders, colon separated>

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
  "$convert" --quiet --overwrite --format both "$folder" "$out" || [ $? -eq 1 ]
  for file in "$out"/*.dng "$out"/*.tif; do
    [ -f "$file" ] || continue
    converted=$((converted + 1))
    report=$("$exiftool" -validate -warning -error -a "$file")
    if ! grep -q "Validate *: OK" <<< "$report"; then
      echo "$file:" >&2
      echo "$report" >&2
      exit 1
    fi
  done
done
echo "$converted DNG and TIFF files valid"
