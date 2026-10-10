#!/bin/bash -e

# Download the sample RAW files the integration tests convert, into ./samples.
# They come from https://raw.pixls.us, all under the CC0 (public domain)
# licence, and are checked against the SHA-256 the site publishes. A file that
# is already there and matches is not downloaded again.

cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p samples/adobe

# <file name> <raw.pixls.us id> <sha256>
SAMPLES=(
  "Canon_EOS_5D_Mark_II_1.CR2 1182 81c5ee6da45c4e22584c5344807ac32e2c9ba82ad274486ba64745ef8dd4f69c"
  "Canon_EOS_5D_Mark_II_2.CR2 1593 12b3ad6aed9fbcd19ba3bfffcd0769aa5a041d610205eee840bf4b2c420dffae"
  "Canon_EOS_5D_Mark_II_sRAW1.CR2 1594 acadf73d760b1f311f18f473b859e262ef3ba1513b3360f02675c38dcadba80b"
  "Canon_EOS_5D_Mark_II_sRAW2.CR2 1595 e8a31e7dfa7d5739f26da1049b1bbe78982d25004e0e857209f32d2e308c4a0c"
  "Canon_EOS_450D.CR2 1353 e975f71429b939701dd618a7bac4709c1409f4bb6aa1c3607ceeaa86031c1c4a"
  "Canon_EOS_Digital_Rebel_XSi.CR2 1202 f30828b387767250e4197bc273c32fc16f1abc993af9d644e9a35a95e5e030ee"
  # A raw file and the DNG Adobe DNG Converter 9.8 made of it (lossless
  # JPEG): the reference the converter's output is compared with.
  "Canon_EOS_5D_Mark_III.CR2 771 ec069b178b9383d80c72f8cdc1b79bd54ca451e7c20793b109d8ad73bef6bdf6"
  "adobe/Canon_EOS_5D_Mark_III.dng 1024 3118116735d4f6dd01fd9d6a80ee9aac52a2c2debdc6dcd112bd5e3059044a55"
)

for sample in "${SAMPLES[@]}"; do
  read -r file id sha <<< "$sample"
  target="samples/$file"
  if [ -f "$target" ] && echo "$sha  $target" | sha256sum --check --status; then
    echo "Up to date: $target"
    continue
  fi
  echo "Downloading $target"
  # getfile.php only needs the id: the rest of the URL is the file name.
  curl --fail --location --silent --show-error --retry 3 -o "$target.part" "https://raw.pixls.us/getfile.php/$id/nice/sample"
  if ! echo "$sha  $target.part" | sha256sum --check --status; then
    echo "Checksum mismatch for $target" >&2
    rm -f "$target.part"
    exit 1
  fi
  mv "$target.part" "$target"
done
