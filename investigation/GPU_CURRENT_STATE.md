# GPU current state
Updated: 2026-09-01 (EXP270 confirmed Windows login with retained DCP owner)

## Stable recovery
- Emergency rollback: `EXP-20260828-164-clean-g2-baseline/assisted-boot/`.
  Mu / AML / m1n1 SHA-256:
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `7067ad2fa77e6bb5a7d70586c9bd556344fdabe522200f0caee1a751631080fb` /
  `0f662f15dd651cc169f7996c5eacaba01908087db63a0ddb76937e2f10e82518`.
- Clean GPU-visible inert recovery EXP241, m1n1 / Mu / manifest:
  `0da268f41b06b546f898f53815e39b1b27f8c62e5844b8ea240825f530d2dae2` /
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `9c03e167028696757038d16f465d16c40652c76d0e8875f3cb05c6e41b0252ad`.
- Assisted USB only; never install or run `boot.bin` during GPU development.

## Repository identity
- Root `feature/j313-gpu-acceleration`, base `0b13bcc465bce12705c37b12580ce275f899c1ff`, plus working diff.
- m1n1 `e7fba8ab6df027c1089c4cf5f2e121c6fc906ac1`; Mu `5acdb4a7459d6de20bccea5cc1cf14c9f9dea06b`.

## Current GPU package
- Offline accepted package: `EXP-20260831-231-monotonic-receipts`.
- ZIP / SYS / INF / CAT / CER SHA-256:
  `3e3d303254a58f72e1a391c002896ee95913a5c6e0ff1b5654050408b0a2c6e5` /
  `9bfdf1084fb83936b958fc104cddf353184146d86000c31949090a3973031556` /
  `bbd898cd90f426044227d9389cee36750893ac1915cd617639d736279c00678f` /
  `cb9bfb8bc3f29a16bcac19ea315363337a7f2a4602fa034c7089955b75904b9a` /
  `97145866a1530003077eacd8457f1a7a644d662423278fd94e450f903c85cbda`.
- No GPU package is installed or carried into EXP245/EXP255.

## Proven lifecycle boundary
- Windows admission through `StartDevice` is proven by EXP231; StartDevice intentionally fails closed.
- Integrated render chain is offline-only: Windows FIFO -> worker -> backend -> EXP208 -> TA+3D -> dual completion -> exact Windows fence.
- DCP EPMAP advertises `0x37`; AP power is ON; START alone survives 500 ms. EXP246 proves SID4 before RTKit, EXP247 proves corrected SET_SHMEM / InitComplete, EXP252 admits A401 after D576 and properties, EXP253 admits A426, EXP255 admits A449, EXP256 admits A456, and EXP257 proves A411 returns main-display true on the canonical baseline.
- EXP270 hardware-proves one retained full IOMFB owner through HV Scanout ABI
  v2, A408 APPLIED, exact D589, Mu, all eight guest CPUs and the Windows lock
  screen. No GPU package or Windows-driven present was used.

## Last experiment and verdict
- EXP254 is **SUPERSEDED BEFORE RUN**: its frozen source omitted TraceKit and
  canonical system-endpoint ordering and retained the obsolete traffic-drain
  discriminator in source.
- EXP258 confirms A472; EXP259 confirms A410; EXP260 rejects the old A412
  zero-result assumption. None may be repeated; exact evidence is in ledger.
- EXP261 proves A412 transport/result; EXP262 confirms strict A412 admission.
  Both are final/no-retry; exact artifacts and receipts are in the ledger.
- EXP263 **REJECTED; NO RETRY**. m1n1 / Mu / manifest / source:
  `d35c9dfe15acc909a83c76ff7bc63fec226c280543145fe5650753351acfff36` /
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `1dee5466ea6b3cfb40075535e6d5a7a48f3a0e60aa5b5e91c435ef952665bf14` /
  `a4cdf4eacb1ed232778082cf5e183df99d8f0d0d4fd7a99a4cfa9834121e772c`.
- EXP263 stage-1 / chainload / live-proxy receipt SHA-256:
  `410607c8656b44336d4912a4be7c50d7fb797db866a083134a293c9a16f2c108` /
  `47786845b52b13e2249e2d54c1be205eb6a06fdb97c0d7b0a81d3888a71695a6` /
  `888976aa5ef9b3239cb35fce44469819fb35f64a695109e928e1d66b9df57e80`.
- EXP264 **REJECTED; NO RETRY**. m1n1 / Mu / manifest / source:
  `0f09d17abafd6fccbc3f30f1f89bd78aafe69330f9cd45d46571201e524fe21d` /
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `49f185eaefae26e71374b50c6616c805b473d6995068b02ee377a274a5ac2abd` /
  `fc7f5615f21a4b1d7b1caa684ca79f53ed19e0f31abdd6241c35c43cd1d2104c`.
- EXP264 stage-1 / chainload / live-proxy receipt SHA-256:
  `f6c8ffd14e52575dac6c88539e0343fdee098a07dba3f21c890bb92264f02b67` /
  `90aa06489add7163550000d5e25e84c649ae7e51f5b7f29c0e894d701d5bfd7a` /
  `380d130272604c39afd6ff7d1cb9ecb08908d5d6c03c22928af3ed11bebfbb19`.
- Exact receipts: A441 `transport=1 result=0`, A412 `transport=1 result=2`,
  then bounded `DPTimingModeId` timeout, controlled quiesce and live 8-CPU proxy.
- EXP265 is **INVALID BEFORE RUN / SUPERSEDED**: stale parsed ADT type made
  chainload fail before `push_adt`; its m1n1 never executed.
- EXP266 **REJECTED; NO RETRY**. m1n1 / Mu / manifest / source:
  `fc56c43ff233b01c748e2b3ddc1411a5837d38fc152fdfad9bacf899fd50bb22` /
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `ab66a1aa23f346bce53eb2fa806a436e04377b8276f2702e43bb72b6b9f3d899` /
  `4df7c8c19bc5581f0d6edbf3cf925a0c37d986bf16e08bfd9d18e09f14669d76`.
- Exact chainload / live-proxy receipt SHA-256:
  `420294aec37b03d135aa7de5be6465cc2c456875e9c0f57c2fda8b7460dde526` /
  `e049c800de64841fa8286d05442b44e49f04d37928944d92874328580dc906ff`.
- Initial and one reissued A412 both returned transport `1`, result `2`; no
  D563 timing publication followed. Controlled quiesce returned a live proxy.
- EXP267 **CONFIRMED; NO RETRY**. Exact m1n1 / Mu / manifest / source:
  `71e9a8189726f599c5f3c4b2890a26a78c282e59bac2e930ad4c5406def16461` /
  `e8312e967604dbbac4780a50ed37dea98a25aea45cbfc8636535fad04c3be074` /
  `8a6f57676e6958f41d392b46d7bb54527a76ab94b790c5753b1dc7f0669fdbc3` /
  `61833d077cf7facf6cc699a498fa5c4a15be4be6acdbd55c4decf57836b7d3c8`.
- Hardware receipts: A407/A408 accepted, `A408 APPLIED swap_id=8`, exact
  `D589 latch swap_id=8`, then controlled quiesce and live 8-CPU proxy.
  Chainload / post-run proxy SHA-256:
  `0c6671590c0c4a3157ff62a1b1286f09f32d34ef75ffa42e72d5ba9fdbffcd53` /
  `19896edbf849919a34d2f11886f064a3fe0e514c3cf99a1b7fe6e98af2f64e04`.

## Key rejected hypotheses
- START crash is not caused by iBoot/dual ownership, mandatory system EP, AP-power ACK, missing EPMAP, START alone, START/mapping order, or pending system traffic.
- NVMe Event129 is not a GPU-package symptom; EXP241 fixed level-INTx re-delivery and qualified the inert baseline.
- Scanout needs DISP + DCP DART mappings, not an extra PIODMA SID4 mapping. PIODMA is only for explicitly requested firmware buffers.
- Mu does not modeset/release DCP; GOP republishes the inherited BootArgs FB. Hidden Mu framebuffer/mode conflict is rejected.
- D411 map-reg ABI/bounds match pinned Asahi v13.5.

## Offline-proven downstream blockers
- `SupportNonVGA=1` is tied to exact POST restore, D589 latch and full stop; focused test GREEN.
- Keep `PreemptionAware=0`: current physical-patching design has no documented alternative that truthfully supports preemption without a real context/cancel/retire lifecycle.
- ARM64 UMD is admission-only (`OpenAdapter10_2` returns `E_NOTIMPL`); DirectFlip/useful D3D cannot yet be claimed.
- Working m1n1 strictly selects canonical DCPAV color/timing modes and now
  follows pinned A472 -> A410 -> A441 -> A412 panel sequencing. Parser/ABI/lifecycle
  and full host suites plus the A472 observer build are GREEN.
- Truthful remaining debt: `FlipIndependent`, DirectFlip/UMD, PerEngineTDR, GDI KCB and preemption/recovery; never enable isolated cap bits.

## Active hypothesis
- EXP254 is `SUPERSEDED BEFORE RUN` and forbidden. EXP267 proves the canonical
  inherited-framebuffer A407/A408 transaction reaches exact D589 latch. EXP268
  proves initial full-owner bootstrap/quiesce succeeds but immediate same-stage
  DCP reopen times out. EXP269 proved retention through D589 but was
  harness-inconclusive. EXP270 with the bit-identical candidate and persistent
  foreground runner reaches the Windows lock screen. The retained-owner host
  lifecycle is confirmed; the first unknown is Windows AppleAgx production
  StartDevice consuming the already-live platform owner without a workload.

## Next offline action
- Freeze EXP270 as the new assisted DCP/Windows recovery point. Then verify the
  current AppleAgx package's boot-start platform-owner/Scanout ABI handshake,
  fail-closed no-workload behavior, WDK build/sign/hashes and clean install plan.

## Next allowed hardware action
- No repeat of EXP268/269/270. After the package gate, one boot-start admission
  run may test `DriverEntry -> AddDevice -> StartDevice -> platform-owner
  attach` only. No render submission or repeated flip. Host rollback is EXP270;
  package cleanup must restore the exact driver-free baseline.
