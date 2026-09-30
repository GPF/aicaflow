# Asset licences

Code is MIT unless a file says otherwise.

- `tools/author/afx_ya2beam.c` is a CC0-derived full-buffer AICA-ADPCM encoder. It
  is built locally by the Python compatibility tool when a host C compiler is
  available.

- `examples/dsp_effects_player/sources/wilhelm_scream.pcm` is the deterministic
  PCM conversion of Wikimedia Commons' CC0 Wilhelm Scream source. Its source
  URL and input SHA-256 are in `WILHELM_SCREAM.md`.
- `examples/dynamic_sfx/assets/drxlax_thrust_red_loop.pcm` and
  `drxlax_mine_slide.pcm` are released by their creator under CC0.

The optional classical-player SoundFont and its musical source material are
downloaded by an explicit asset-fetch step. Their exact URLs, hashes and
licences are recorded with that example; they are not part of this repository.
