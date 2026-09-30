# Persistent Tuner

`examples/tuner_server` stays resident on a BBA-equipped Dreamcast and accepts
prepared asset uploads over TCP. It is a development tool: ARM7 still executes
only validated AFX commands and never interprets MIDI, profiles or source media.

The wire protocol uses a fixed 16-byte little-endian header: magic `AFT1`,
version, opcode, sequence and payload size. The host client is
`tools/afx_tuner_client.py`. Load the tuner once with a Dreamcast loader, then
upload AFB/AFX pairs while iterating on authoring inputs.
