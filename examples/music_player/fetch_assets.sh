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
fetch grieg-mountain-king-orchestra.mid 'https://midicities.com/ViewGeocitiesSong?fileType=midi&handler=DownloadFileGeocities&token=4775baed-987d-44c4-9de9-403637059e9b' 5eeafac3e502868db9f983f80e4d747ed38b8fd31cc577cadb5a395f78b5e6f4
# This is the SoftSynth-oriented GeneralUser-GS bank used by the approved
# pre-C classical rendering. Its simpler modulation programming is the right
# source for AICA's offline lowering, and the hash pins the exact old tone.
fetch GeneralUser-GS.sf2 https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/GeneralUser-GS.sf2 9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe

# Ethan Winer's original solo cello is royalty-free, including commercial use.
# Keep the published archive only as a fetch transport; the SF2 hash pins the
# exact input passed to the native authoring tool.
cello_zip=$dest/cello_solo.zip
cello=$dest/cello_solo.sf2
cello_sha=2297e81c080d5383b21caefd71e5924c9f75c6f0e1b1e518a08686ebe603f596
if [ ! -f "$cello" ] || [ "$(digest "$cello")" != "$cello_sha" ]; then
    fetch cello_solo.zip https://ethanwiner.com/cello_solo.zip e130f0cc522cb3ff473b61328c6e8808fc2b71308a0f2009c583bcc6c9b0c0a6
    unzip -p "$cello_zip" cello_solo.sf2 > "$cello.part"
    actual=$(digest "$cello.part")
    [ "$actual" = "$cello_sha" ] || { printf 'cello_solo.sf2: expected %s, got %s\n' "$cello_sha" "$actual" >&2; exit 1; }
    mv "$cello.part" "$cello"
    printf 'extracted cello_solo.sf2\n'
else
    printf 'verified cello_solo.sf2\n'
fi

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
