
### Screen messages

Protocol v1 opcode17 (`MESSAGE`) accepts1..79 printable ASCII bytes without NUL/newline and appends them to the on-screen terminal. Invalid payloads return BAD_COMMAND without displaying text. PING advertises the current maximum opcode; earlier opcodes remain unchanged. The existing font path is ASCII, so use transliterated titles where needed.

    python3 tools/afx_tuner_client.py --host 10.0.0.184 message --text "Afspiller Monkey Island Theme"

The capture tool accepts an optional `message` argument and sends it after PLAY. `build/renditions/measure.py` uses this to identify the candidate and DSP-room state. Messages are display-only and do not change AFX playback state.

### Shared-bank songs

The tuner can also audition a production AFB and its matching bank-bound AFX
control files without loading the game:

    python3 tools/afx_tuner_client.py bank-upload --file build/dc/aicaflow/music.afb
    python3 tools/afx_tuner_client.py control-upload-play \
      --file build/dc/aicaflow/music_controls/sequence_7.afx \
      --index build/dc/aicaflow/music_controls/sequence_7.afc

The bank stays resident while subsequent `control-upload-play` commands swap songs.
`--index` is optional: it attaches the matching SH-4-only AFC before playback
restarts, enabling the `region` command without uploading any extra data to AICA.
