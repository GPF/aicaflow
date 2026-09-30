#!/usr/bin/env python3
"""Stage the small, reproducible input library for the DSP effects player."""

from __future__ import annotations

import sys
from io import BytesIO
from pathlib import Path

import mido

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import afx_compile
import afx_midi


def midi_bytes(name: str, channels: tuple[tuple[int, tuple[int, ...]], ...]) -> bytes:
    midi = mido.MidiFile(ticks_per_beat=480)
    for channel, notes in channels:
        track = mido.MidiTrack()
        midi.tracks.append(track)
        track.append(mido.Message("program_change", channel=channel, program=channel, time=0))
        track.append(mido.Message("control_change", channel=channel, control=91, value=127, time=0))
        for note in notes:
            track.append(mido.Message("note_on", channel=channel, note=note, velocity=110, time=240))
            track.append(mido.Message("note_off", channel=channel, note=note, velocity=0, time=180))
        track.append(mido.MetaMessage("end_of_track", time=1920))
    out = BytesIO()
    midi.save(file=out)
    return out.getvalue()


def sine_mapping(controls: int) -> dict:
    instruments = {"0:0:0": {
        "kind": "sine", "root_key": 69, "velocity": "fixed", "release": "keyoff",
        "midi_reverb": "apply",
    }}
    for program in range(1, controls + 1):
        instruments[f"0:0:{program}"] = {
            "kind": "sine", "root_key": 69, "velocity": "fixed", "release": "keyoff",
            "direct": 0x0010, "dsp_send": 0xF0,
        }
    return {"instruments": instruments}


def compile_sine(name: str, channels: tuple[tuple[int, tuple[int, ...]], ...]) -> bytes:
    source = midi_bytes(name, channels)
    timeline = afx_midi.parse_bytes(source, name)
    return afx_compile.compile_timeline(timeline, sine_mapping(len(channels) - 1), tick_rate=1000)[0]


def compile_wilhelm(pcm: bytes) -> bytes:
    midi = mido.MidiFile(ticks_per_beat=480)
    track = mido.MidiTrack()
    midi.tracks.append(track)
    track.extend((
        mido.Message("program_change", channel=0, program=0, time=0),
        mido.Message("control_change", channel=0, control=91, value=127, time=0),
        mido.Message("note_on", channel=0, note=69, velocity=127, time=960),
        mido.Message("note_off", channel=0, note=69, velocity=0, time=1920),
        mido.MetaMessage("end_of_track", time=0),
    ))
    encoded = BytesIO()
    midi.save(file=encoded)
    mapping = {"instruments": {"0:0:0": {
        "kind": "raw_pcm16", "velocity": "fixed", "release": "keyoff",
        "attack_rate": 31, "decay_rate": 0, "decay_level": 31, "sustain_decay_rate": 0,
        "release_rate": 31, "direct": 0x0F10, "dsp_send": 0xF0,
        "zones": [{"pcm16_bytes": pcm, "source_rate": 22050, "resample_hz": 22050, "root_key": 69}],
    }}}
    timeline = afx_midi.parse_bytes(encoded.getvalue(), "Wilhelm scream")
    return afx_compile.compile_timeline(timeline, mapping, tick_rate=1000)[0]


def stage(output: Path, name: str, timeline: dict, mapping: dict) -> None:
    bank, flow, seek, _ = afx_compile.compile_bank_flow_timeline(timeline, mapping, tick_rate=1000)
    (output / f"{name}.afb").write_bytes(bank)
    (output / f"{name}.afx").write_bytes(flow)
    if seek:
        (output / f"{name}.afc").write_bytes(seek)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        raise SystemExit("usage: prepare.py output-dir wilhelm-scream.pcm")
    output, scream = map(Path, argv)
    output.mkdir(parents=True, exist_ok=True)
    inputs = {
        "effect": ("effect-tuned", ((0, (72, 76, 79, 84)), (1, (24,)), (2, (31,)), (3, (36,)), (4, (43,)))),
        "impulse": ("impulse", ((0, (96,)),)),
        "tone": ("tone", ((0, (69,)),)),
        "modulated": ("modulated tone", ((0, (72, 76, 79, 84)), (1, (36,)))),
    }
    for name, (title, channels) in inputs.items():
        stage(output, name, afx_midi.parse_bytes(midi_bytes(title, channels), title), sine_mapping(len(channels) - 1))
    pcm = scream.read_bytes()
    midi = mido.MidiFile(ticks_per_beat=480)
    track = mido.MidiTrack(); midi.tracks.append(track)
    track.extend((mido.Message("program_change", channel=0, program=0, time=0),
                  mido.Message("control_change", channel=0, control=91, value=127, time=0),
                  mido.Message("note_on", channel=0, note=69, velocity=127, time=960),
                  mido.Message("note_off", channel=0, note=69, velocity=0, time=1920),
                  mido.MetaMessage("end_of_track", time=0)))
    encoded = BytesIO(); midi.save(file=encoded)
    mapping = {"instruments": {"0:0:0": {"kind": "raw_pcm16", "velocity": "fixed", "release": "keyoff",
        "attack_rate": 31, "decay_rate": 0, "decay_level": 31, "sustain_decay_rate": 0, "release_rate": 31,
        "direct": 0x0F10, "dsp_send": 0xF0,
        "zones": [{"pcm16_bytes": pcm, "source_rate": 22050, "resample_hz": 22050, "root_key": 69}]}}}
    stage(output, "wilhelm", afx_midi.parse_bytes(encoded.getvalue(), "Wilhelm scream"), mapping)
    print(f"staged DSP inputs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
