# Integration

Include `<aicaflow/host.h>` and `<aicaflow/bank.h>`, embed
`firmware/aicaflow.drv`, then initialize AICAflow once:

```c
alignas(32) static const unsigned char firmware[] = {
#embed "firmware/aicaflow.drv"
};

afx_init(firmware, sizeof(firmware));
```

Load one AFB into an `afx_bank_t`, upload an AFX bound to that bank, then
activate the resulting flow. Call `afx_update()` regularly and recycle finished
instances. `afx_shutdown()` is only valid after flows, instances and banks have
been released.

See [lifetime.md](lifetime.md) for the ownership rules and
[formats.md](formats.md) for the asset contract.
