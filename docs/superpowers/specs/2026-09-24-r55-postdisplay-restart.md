# R55: POST ownership on repeated StartDevice

## Sources and observation

- EXP775 ETL `hardware-evidence/posttrace/dxgkrnl_000001.etl`: the cold boot
  completed StartDevice and failed later at AddAdapter; one live restart failed
  at StartDevice Stage9 with `0xc01e0002`. Existing receipts cannot tell whether
  the acquire callback returned that status or the driver rejected its geometry.
- Pinned WDK26100 `dispmprt.h` interface is used by `lifecycle.c` and
  `display.c`; current `scanout_windows.c` starts the fixed J313 mode through
  the m1n1 DCP/IOMFB Scanout ABI v2 from its own validated pool, independently
  of `PostDisplayInformation`. Mu and m1n1 launch bytes are unchanged.
- [Microsoft Learn: acquire POST ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_acquire_post_display_ownership)
  explicitly permits successful acquisition with `Width=0`, meaning no POST
  information is available. A failed callback is a real failure, not this case.
- [Microsoft Learn: stop and release POST ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_stop_device_and_release_post_display_ownership)
  requires truthful framebuffer handoff; if this DDI fails, dxgkrnl calls
  `DxgkDdiStopDevice`.

## Contract

The KMD records the raw callback status and geometry separately from its
decision. On success with a valid J313 POST mode, it adopts that mode. On
success with `Width=0`, it leaves POST information empty and starts its own
fixed panel mode through the qualified Scanout ABI v2. It must not fabricate
a POST physical address. If the callback fails, or a nonzero POST mode has
invalid geometry, StartDevice fails closed. Without POST information,
`StopDeviceAndReleasePostDisplayOwnership` returns `STATUS_NOT_SUPPORTED` so
dxgkrnl uses normal StopDevice; no freed scanout pool is handed to the OS.

The next falsifiable checkpoint is a single separately armed live restart:
`Wom1PostDisplayRoute` must show raw callback status and chosen route, then
StartDevice must complete if the route is the documented `Width=0` case.
If the callback itself failed, this change deliberately preserves that failure
and the receipt identifies the next owner. Recovery remains the hash-pinned
GPU-hidden image or the R54 full-owner series path while SSH and package
identity remain certain.
