# Tools

`author/` is the supported native C authoring path. `make compiler` builds
`build/afx_compile_c`, which writes one strict AFB, its sample-free AFX, and
the matching AFC and AFV sidecars.

`tuner/` contains the resident Dreamcast tuner (`server/`) and its small host
client (`client.py`). It is a development tool, not an application example.

`test/` contains fixtures and checks. `research/` contains the older Python
experiments and reference implementation. They remain useful for comparison
and investigation, but examples must not gain a new dependency on them.
