# Persistent Tuner

`tools/tuner_server` stays resident on a BBA-equipped Dreamcast and accepts
prepared asset uploads over TCP. It is a development tool: ARM7 still executes
only validated AFX commands and never interprets MIDI, profiles or source media.

Build it separately from the application examples:

```sh
source /opt/toolchains/dc/kos/environ.sh
make tools
```

The wire protocol uses a fixed 16-byte little-endian header: magic `AFT1`,
version, opcode, sequence and payload size. The host client is
`tools/afx_tuner_client.py`. Load the tuner once with a Dreamcast loader, then
upload AFB/AFX pairs while iterating on authoring inputs.

For example, upload a bank and its matching control flow (the AFC index is
optional and stays on SH4 for seek commands):

```sh
python3 tools/afx_tuner_client.py bank-upload --file music.afb
python3 tools/afx_tuner_client.py control-upload-play --file track.afx --index track.afc
```
