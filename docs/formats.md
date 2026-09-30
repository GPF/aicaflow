# Asset formats

Every sampled flow is exactly one AFB plus one AFX. The ARM7 never parses either
file: SH4 validates them, uploads the contiguous AFB payload once and resolves
the AFX's bank-relative addresses before activation.

- `.afb` — one 32-byte header followed by the encoded sample bytes.
- `.afx` — one fixed header, AICA setup image, relocations and timed command
  stream; it names exactly one AFB identity.
- `.afc` — optional SH4-only seek checkpoints for one exact AFX/AFB pair.
- `.afv` — optional player visualisation sidecar; never consumed by the driver.
- `.afp` — offline performance profile bound to one exact base AFX by
  SHA-256. It owns DSP scene choice and DSP-send level as well as future
  per-tone template assignments; it produces a derived AFX and is never
  uploaded to Dreamcast.
- `.afi` — optional offline AFB sample-offset index; it is not needed to play
  an AFX and is never sent to ARM7.
- `.afbm` — editable offline bank map: SoundFont sources, MIDI-program to SF2
  preset mappings, their coding policy, and the MIDI flows to build. It
  produces one AFB and matching AFX/AFC/AFV files; it is never sent to ARM7.

AFB/AFC identity headers and the AFX wire layout are little-endian and defined
by `driver/include/aicaflow/protocol.h` and `driver/include/aicaflow/codec.h`.
The AFB header is 32 bytes. AFX files never embed samples, and AFB files have no
runtime sample-name or sample-index table.
