# Independent C20 and C21 hardware checkpoints

The user-authorized throughput rule permits separate fresh-device steps in one
package. After EXP1390 C19 acceptance and exact durable rollback, run the already
built EXP1392 source30381c63/probe73ac69ac package with C20A/B and C21A-E as
independent variables. Planned EXP1391 hardware is superseded, its successful
build receipt remains unchanged. No active run or source artifact is changed.

Primary implementation checks: C20 owner commit376a0024 modifies only the byte
span in bind_stage's nontexture PIPE_BUFFER sampler-view branch; typed R8/R16
spans retain trailing bytes, raw/structured spans keep dword rounding. C21
commit3de512cb adds bounded Texture3D policy, native layout query/create/reflection,
mip/Z copies, 3D SRV/UAV/RTV and 3D shader metadata. Its texture SRV branch exits
before the C20 buffer extent branch. Buffer allocation/reflection behavior is
covered by existing deterministic suites. These are independent operations, not
a claimed Microsoft atomic invariant. C21's own query/create/reflection fields
remain one documented indivisible invariant.

The probe unconditionally calls run_isolated for C20A/B and C21A-E, using a fresh
device per step and ignoring the previous step's returned status. Each has its
own AGX_D3D12_<step> PASS/FAIL plus readback and UMD receipts. A normal reported
failure does not suppress the other steps. A process crash or guest hang still
makes unreached steps inconclusive and requires recovery/new ID.

C20A: three R8_UINT values17/83/197 and GetDimensions3 at64B origin. C20B:
three R16_UINT values257/4099/60013 and GetDimensions3; sentinels preserved.
C21A-E: the already preregistered bounded native3D copy/load/store/RTV/sample
oracles, with coherent legacy two-pattern metadata. All45 accepted predecessor
D3D12 lines remain controls; total52 expected. Record per-step verdicts.

Recovery is the same factoryV2 GPU-visible service-disabled exact-package
controller. Require accepted EXP1390 and RECOVERY_DONE/PackageAbsent before
launch; no retained package or automatic launch. m1n1/Mu/ICD/WOWUMD unchanged.

Frozen queued script hashes (backups retain pre-bundle originals):

- `chain1392.sh`: `4d09ce85469988ad970ea61921bf521020e9ac7b872735b2d7906fcc0957dd4f`
- `newexp-kmd1392.sh`: `9c7455699ca8d663dc75cd75cc55e95d6b7afcd4ad3d75a0de8976f1b0125b6e`
- `before1392.txt`: `357fea05f1332dd13d638ede2982f8ee6db6c6f527b1e12f28e3637b75e4539f`
