# Native UMD contract boundary after EXP916

This is an offline design prerequisite, not authorization to advertise additional
DDIs and not a proven diagnosis of DWM's internal failure branch.

WHY CONTINUE COMPARISON: no historical comparison is needed. Current native source
explicitly supports only D3D10 DDI; current official Microsoft requirements and
pinned WDK define a broader FULL GRAPHICS contract. Resolve that deterministic
mismatch before another capability experiment. Do not revisit AGX MMIO, power,
RTKit, interrupt routing or Mu topology to explain a missing UMD interface.

WINDOWS CONTRACT: FULL GRAPHICS WDDM3.0, feature level10.0 is distinct from UMD DDI
version. Classify each required D3D11.1/DXGI1.2 entry and relevant later WDDM3
requirement using pinned WDK26100 and official Microsoft documentation. Account
separately for the documented D3D9 sharing requirement and current
NO_REDIRECTION behavior. Neither a feature-level bit nor a callback address
constitutes implementation.

AGX/ASAHI CONTRACT: retain the proven existing Mesa/Asahi resource, shader, UAT,
queue and fence owners. Current experiment changes no ADT/MMIO/IRQ/DMA/power
contract. Before implementing a new operation, read its current Asahi/Mesa
implementation and map exact resource layout, range, synchronization and lifetime
semantics. External source can inform behavior; do not copy code without the
repository's explicit license compatibility review. m1n1 source9320da31 and
Mu0dac687/R143 remain downstream references; no firmware change is proposed.

TRANSLATION:
1. Produce a typed inventory from WDK's actual D3D11_1DDI_DEVICEFUNCS and
   DXGI1_2_DDI_BASE_FUNCTIONS. For every required slot identify an existing real
   native implementation or a missing owning implementation. Include ABI-changed
   existing slots; a table memcpy/cast from D3D10 is invalid.
2. Implement the versioned frontend without publishing it until all mandatory
   companion behavior is complete. Reuse only signature-compatible functions.
   Design constant-buffer offset/range binding, partial updates/copies, resource
   creation/opening/sharing, views/ClearView, discard and residency offer/reclaim
   as separate deterministic changes, each with its owning state machine.
3. Derive the full presentation path: runtime DXGI context and allocation identity
   -> native resource ownership/flush -> kernel Present or supported virtual
   submission -> completed output -> existing scanout owner. Distinguish
   redirected client Presents from physical desktop scanout. Derive D3D9 sharing
   or another explicitly Microsoft-supported architecture before changing
   NO_REDIRECTION. Do not conflate DXGI1.2 Blt1 with DXGI1.3 Present1.
4. Verify the real production functions offline with the pinned ABI and real
   adapter/device tables. Tests must catch concrete range, layout, ownership,
   lifetime, data-copy or completion bugs; no artificial RED for documentation.
   Include table-size canaries, wrong-version rejection, changed-signature
   invocations and byte-exact resource operations. Native ARM64 build must be
   warning-free; preserve source archive, PDB identity, signing and hashes.
5. Only after that offline gate may one preregister a hardware checkpoint that
   observes actual negotiated interface, accepted desktop output, completed
   Present, and a changing nonzero scanout over the stability window. Code0 and
   pfnPresentCb S_OK alone do not satisfy it. Keep the exact package experiment
   local and roll back in ordinary GPU-visible Code43 before another package.

WHAT IS STILL UNKNOWN: which exact current DWM internal branch prevents desktop
composition/output; whether closing the interface/resource contract will suffice;
which additional modern mandatory operations are absent from the native path.
The current evidence proves the completeness gap, not the immediate causal link.
No new hardware run or version/capability advertisement has been prepared.

Ownership: frontend owns Windows ABI and resource/view semantics; UMD winsys owns
translation and lifetime; KMD owns residency, scheduling, interrupts and completion;
m1n1 broker owns validated AGX/scanout transport and recovery; Mu owns exposed ACPI
and initial display handoff. Fix a violated contract in its owner.

Inspected files/specifications: current build-native-asahi-state.py native
Adapter/Device/State/DXGI adaptations; render-admission/umd/src/umd.c native
OpenAdapter10_2 branch; pinned Mesa frontends/d3d10umd/{Adapter,Device,DxgiFns}.cpp
and State.h; pinned wdk26100/{d3d10umddi,dxgiddi}.h; Microsoft Supporting the DXGI
DDI, Direct3D software requirements, and WDDM1.2 feature matrix (links in EXP916
verdict). Recovery artifacts: immutable EXP377 fae3444c + EXP392 16c17718;
EXP385 279bd36a remains emergency only.
