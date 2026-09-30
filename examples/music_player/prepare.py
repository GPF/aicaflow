#!/usr/bin/env python3
"""Compile the three declared MIDI sources to AFB/AFX/AFC/AFV player assets."""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import afx_compile
import afx_metadata
import afx_midi
import afx_visualize

SONGS = (
    ("chopin-op28-no20", "Chopin - Prelude Op. 28 No. 20"),
    ("bach-bwv1007-prelude", "Bach - Cello Suite 1 Prelude"),
    ("grieg-mountain-king", "Grieg - Hall of the Mountain King"),
)
MAX_BANK_BYTES = 0x1D8000


def mapping(timeline: dict, soundfont: Path) -> dict:
    instruments = {}
    for note in timeline["notes"]:
        key = f"{note['bank_msb']}:{note['bank_lsb']}:{note['program']}"
        if key in instruments:
            continue
        instruments[key] = {
            "kind": "sf2_pcm16", "soundfont": str(soundfont),
            "preset": f"0:{note['program']}", "channel": "stereo",
            "velocity": "sf2", "release": "keyoff",
            "envelope": "aica_sf2_adsr_v1", "sample_format": "pcm8",
            "resample_hz": 44100, "tail_ms": 200, "fit_sample_frames": True,
            "attenuation_centibels": 1200, "mix": 0x24, "direct": 0x0F10,
            "gain_model": "fluidsynth_2", "pan_model": "sf2",
            "filter_model": "sf2_static", "filter_offset_cents": 600,
        }
    return {"tick_rate": 1000, "instruments": instruments}


def source_path(name: str) -> Path:
    return ROOT / "examples" / "music_player" / "sources" / f"{name}.mid"


def song_header(items: list[dict]) -> str:
    fields = ("bytes", "sample_bytes", "stream_bytes", "setup_bytes", "padding_bytes",
              "command_baseline_bytes", "note_count", "sample_count", "setup_count", "channel_count")
    rows = []
    for item in items:
        values = ",".join(str(item[field]) for field in fields)
        rows.append("{" + ",".join((json.dumps(item["title"]), json.dumps(item["file"]),
                   json.dumps(item["bank"]), "NULL", "false", values)) + "}")
    return ("static const struct song { const char *title, *file, *bank, *dsp; bool wraps; "
            "uint32_t bytes, sample_bytes, stream_bytes, setup_bytes, padding_bytes, "
            "command_baseline_bytes, note_count; uint16_t sample_count, setup_count; "
            "uint8_t channel_count; } songs[] = {\n" + ",\n".join(rows) + "\n};\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--soundfont", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    soundfont = args.soundfont.resolve()
    if not soundfont.is_file():
        raise SystemExit(f"missing SoundFont: {soundfont}")
    # GeneralUser contains a few unrelated legacy drum/SFX loops which sf2utils
    # warns about while opening the bank; none are selected by these three scores.
    logging.disable(logging.WARNING)
    args.output.mkdir(parents=True, exist_ok=True)
    compiled = []
    for name, title in SONGS:
        timeline = afx_midi.parse_bytes(source_path(name).read_bytes())
        bank, flow, seek, diagnostics = afx_compile.compile_bank_flow_timeline(
            timeline, mapping(timeline, soundfont), 1000)
        if len(bank) > MAX_BANK_BYTES:
            raise SystemExit(f"{name}: {len(bank)} byte AFB exceeds {MAX_BANK_BYTES} byte AICA budget")
        if not seek:
            raise SystemExit(f"{name}: expected a seek index")
        (args.output / f"{name}.afb").write_bytes(bank)
        (args.output / f"{name}.afx").write_bytes(flow)
        (args.output / f"{name}.afc").write_bytes(seek)
        afx_visualize.write(args.output / f"{name}.afx", args.output / f"{name}.afv")
        layout = afx_metadata.resident_layout(flow)
        compiled.append({"title": title, "file": f"{name}.afx", "bank": f"{name}.afb",
                         "bytes": layout["image_bytes"] + len(bank) - 32, **layout,
                         "source_sha256": hashlib.sha256(source_path(name).read_bytes()).hexdigest(),
                         "bank_bytes": len(bank), "seek_bytes": len(seek),
                         "diagnostics": diagnostics})
        print(f"{title}: {len(bank)} byte AFB, {len(flow)} byte AFX")
    header = args.output.parent / "include" / "songs.h"
    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(song_header(compiled))
    (args.output / "manifest.json").write_text(json.dumps(compiled, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
