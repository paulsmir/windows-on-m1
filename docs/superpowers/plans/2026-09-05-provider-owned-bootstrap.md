# Provider-owned firmware bootstrap implementation plan

Goal: execute the already-approved CPU/handoff -> locked context0 roots ->
RTKit management -> endpoints -> initdata sequence without platform code
mutating provider ownership. No WDDM/m1n1/Mu/memory architecture change.

Primary sources inspected: Asahi mmu.rs Handoff::init and Uat::new (context0
TTBs under handoff lock); m1n1 agx/__init__.py resume/start; current
apple_agx_rtkit_session.c, apple_agx_firmware.c, firmware_provider.c,
gfx_handoff.c and render-admission/backend_platform_windows.c. EXP463 restores
the exact hardware FirmwareFailed6 boundary with unchanged EXP444 C source.
Current Mu/m1n1 contract remains the frozen EXP441/406 pair with the required
WOM1_AGX_G2_POWER_BROKER=1 launch input; no physical IRQ880--888 enabled.

Implementation decisions within the approved design:

- Keep the generic coordinator BootAsc operation as an atomic bootstrap group,
  but track an explicit ASC cleanup obligation before attempting it. This is
  not proof that ASC started; it prevents partial CPU/root ownership being
  forgotten when a callback fails or completes after its deadline.
- Extend provider primitives with optional CompleteManagementBootstrap. When
  present, BootAsc means CPU_READY/handoff only, provider then publishes roots
  under its own lock and calls CompleteManagementBootstrap. Legacy providers
  lacking this callback retain their existing complete-BootAsc contract.
- Refactor the existing RTKit session boot into two shared functions and have
  its compatibility wrapper call them: no second protocol implementation.
- Only provider code mutates PUBLISHED. StopAsc cleans any early publication
  before stopping ASC; partial stop is idempotent and preserves Running on a
  failed run-off write. Failed unpublication never allows UAT destruction.
- Record provider CPU/root/management/cleanup phase and result at PASSIVE_LEVEL,
  using existing registry receipt style without flushes or additional MMIO.

Verification / execution:

- [ ] RED: existing provider test reproduces running ASC abandoned on handoff
  timeout. Add split-order and failed-unpublish ownership tests exercising real
  provider/coordinator with transport doubles that enforce lock/order.
- [ ] Add cleanup-obligation bit and provider-owned split; refactor session
  boot and adapt only render-admission primitive wiring/receipts.
- [ ] Run provider, firmware, RTKit session, backend/composer and admission
  suites with existing sanitizer harnesses. Test abort/run-off failure leaves
  ownership live, and cleanup retry cannot double-publish or destroy live UAT.
- [ ] Pinned WDK build with PackageBuild464; artifact equality/sign/hash gates;
  commit only coherent source files, append CHANGES row with exact hash.
- [ ] Preregister one natural boot bind on correct broker-enabled platform.
  First falsifiable checkpoint: management begins with provider-owned roots;
  exact provider phase/result separates CPU, publication and RTKit failure.
  Recovery is exact package cleanup in stable guest or EXP377/EXP385 emergency.

Self-review: no unproven caps, no new PMGR call, no m1n1 ownership move; optional
primitive preserves unrelated callers. Cleanup obligation is explicitly
distinguished from completed hardware phase. Existing memory/queue code reused.
