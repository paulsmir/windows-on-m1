# Native v3 General table-source contract

Commit: pending ledger entry for the source-class gate.

The transport now permits a General allocation only as an exact-Read source for
Descriptor, Scissor or DepthBias when Header.Version is exactly native USC v3.
An Encoder source remains accepted for those roles. The General alternative
still needs GpuRead. Shader, ShaderRodata, UscPipeline and Encoder continue to
require their prior source classes. No materializer placement, overlay, caps,
GPUVA or physical-address behavior changed.

RED: the v3 General Descriptor source was rejected by the existing Encoder-only
class gate. GREEN: production transport accepts all three source roles under
the narrow contract. Tests reject General in v1, v2 and version4, a non-Read
reference, missing GpuRead, General USC pipeline and General shader. The
existing Encoder alternative remains accepted. The v3 overlay regression keeps
both overlay planning/binding APIs fail-closed.

Host ASan/UBSan relevant suites pass. Pinned Windows x64 UMD/native-owner
contract build/link/execution passes; ARM64 build/link passes and is not run.
Source archive027 SHA256:
2ab0c0d038847253486022b4d438e61cfd8d745efeed9168f0ba502b85cb5eb7.
x64 executable SHA256:
b6804eb0f0b592b5600daab5f4120f6da7c623a0561d6f5c4794d89c40e37961.
ARM64 executable SHA256:
bfe9c79dcc975c54a0331e46b5066de728d34070b3858ed4e66d890d02ee9fc8.

Raw evidence: investigation/evidence/AD04-native-source-class/.
This has no package, installation, Air or hardware verdict.

The next independent, source-first boundary remains real native sub-4-byte
reference spans (USC 38/62 and rodata 2) versus the universal transport
multiple4 rule. Do not use this source-class result as authorization to relax
that invariant.
