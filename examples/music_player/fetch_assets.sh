#!/bin/sh
# Fetch reproducible scores and the selected default SoundFont.
set -eu

dest=${1:?usage: fetch_assets.sh DIRECTORY}
mkdir -p "$dest"

digest() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}';
    else shasum -a 256 "$1" | awk '{print $1}'; fi
}

fetch() {
    name=$1 url=$2 expected=$3
    file=$dest/$name temp=$file.part
    if [ -f "$file" ] && [ "$(digest "$file")" = "$expected" ]; then
        printf 'verified %s\n' "$name"
        return
    fi
    curl -fL --retry 3 -o "$temp" "$url"
    actual=$(digest "$temp")
    [ "$actual" = "$expected" ] || { printf '%s: expected %s, got %s\n' "$name" "$expected" "$actual" >&2; exit 1; }
    mv "$temp" "$file"
    printf 'fetched %s\n' "$name"
}

# Bernd Krueger's CC-BY-SA 3.0 performance, mirrored at this pinned commit.
fetch chopin-op27-no1.mid https://git.kadet.net/PG/AGU/raw/commit/7c2485ce9b75b7e8b2dad16b7a56d949d4aafa0c/dataset/midi/chpn_op27_1.mid da3484a64d3e3196550e34c7cef537cbc56a4917b423af75127a0833af7ccabb
fetch grieg-mountain-king.mid https://www.mutopiaproject.org/ftp/GriegE/O46/Dans_l_antre_du_roi_de_la_montagne/Dans_l_antre_du_roi_de_la_montagne.mid 0c256a809b5af1faef81b6aad6106088a71d76b12435d5f0775d4ceee60c917e
fetch GeneralUser.sf2 https://raw.githubusercontent.com/ad-si/GeneralUser/master/GeneralUser.sf2 f45b6b4a68b6bf3d792fcbb6d7de24dc701a0f89c5900a21ef3aaece993b839a

bach_zip=$dest/bach-bwv1007.zip
bach=$dest/bach-bwv1007-prelude.mid
bach_sha=c55a2eae5e8d2e7a9570fe7ec8325c8825656893249878b946e6a2ead288a272
if [ ! -f "$bach" ] || [ "$(digest "$bach")" != "$bach_sha" ]; then
    curl -fL --retry 3 -o "$bach_zip" https://www.mutopiaproject.org/ftp/BachJS/BWV1007/bwv1007/bwv1007-mids.zip
    # The full 656-note cello score is the source of the preserved
    # performance-timing work. `bwv1007-1.mid` is a shorter arrangement.
    unzip -p "$bach_zip" bwv1007.mid > "$bach.part"
    actual=$(digest "$bach.part")
    [ "$actual" = "$bach_sha" ] || { printf 'bach-bwv1007-prelude.mid: expected %s, got %s\n' "$bach_sha" "$actual" >&2; exit 1; }
    mv "$bach.part" "$bach"
    printf 'fetched bach-bwv1007-prelude.mid\n'
else
    printf 'verified bach-bwv1007-prelude.mid\n'
fi
