# C15 R1 shader provider admission boundary

Status: source-first decision for the next CPU implementation gate. No typed
shader callback, provider registration, CORE_0010 table, SM6 capability,
package, guest or hardware run is introduced by this note.

WINDOWS CONTRACT: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
defines `D3D12DDIARG_CREATE_SHADER_0010` with root handle, unsized PROGRAM
pointer, IO-signature union, flags and LibraryReference. The exact saved
D3D12Core26100.9549 successful ordinal2/3 path constructs the ordinary
root/code/signature/flags independently of library metadata. It forwards a
DXIL PROGRAM body or SHEX/SHDR payload, not an outer DXBC container. Its
successful DXIL source checks at least eight readable bytes and the declared
word count against the part extent. That runtime-specific observation does
not make arbitrary DDI pointers readable. The first-eight-byte bounded span
helper is now host/native-syntax verified, but it only computes a declared
extent. The official [0010 shader create DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010)
confirms the typed callback shape.

AGX/ASAHI CONTRACT: the original linked `agx_d3d12_shader_owned_core.cpp`
accepts explicit readable PROGRAM/signature/root spans and copies PROGRAM,
fieldwise IO signatures and canonical root bytes into pointer-free CPU
storage. `agx_shader_input_provider.cpp` can register a live device and a
canonical root snapshot, acquire a shader-input lease, then calculate/copy
under a pinned operation. Its producer pointers must remain stable until
lease release. The current AGX owner consumes compiled shaders later; it
does not own this CPU input lifetime. m1n1, Mu, ACPI, UAT, interrupts and
power do not fix an invalid UMD pointer or provider lifetime.

Current DDI gaps, from the actual implementation:

- `CreateDevice` publishes the live identity but does not create/register a
  provider device. `DestroyDevice` retires identity and destroys the native
  owner; it does not quiesce provider callbacks or close a failure sink.
- `CreateRootSignature` fills the current private parameter/range/sampler
  arrays. `DestroyRootSignature` is a no-op. No canonical, padding-free root
  bytes are registered or retired with the provider.
- Current shader calc/create read token DWORD count directly and copy old
  tokens. They do not create a trusted R1 lease, snapshot IO signatures or
  use the owned-core bounds/lifetime transaction.
- The linked zero-cache library checks live same-device LibraryReference,
  but does not make the root, signatures or PROGRAM body live/readable.

TRANSLATION: implement one bounded root-canonical CPU encoder before wiring
any typed shader callback. It must take an explicit trusted byte extent for
the already-created root private storage, serialize semantic fields in a
versioned fixed-endian format (root flags; ordered parameters and resolved
ranges; all static-sampler fields including exact float bits), and reject
count/offset overflow before reading trailing arrays. It must not copy C
padding, raw pointers, a runtime handle or current GPUVA. An encoder result
is an owned byte buffer that the provider can copy during RegisterRoot.
The existing root object's semantic layout is adequate as a source for this
CPU encoder, but its current header does not record the allocation extent;
the future DDI wiring must establish that extent from the actual calc/create
contract rather than inventing one. Provider CreateDevice/RegisterRoot/
RetireRoot/BeginQuiesce/TryQuiesce/Destroy ordering and error callback
reentry must be proved separately before publishing a typed shader table.

WHAT IS STILL UNKNOWN: complete root private-storage bounds/lifetime at
DestroyRootSignature; whether an explicit R1 root handle can be NULL on every
valid shader path; exact IO-signature array readability beyond recorded
counts; provider callback reentry and device teardown; actual converter
internal OOM recovery. The next deterministic test is a CPU encoder fixture:
two roots with different C padding but identical semantic parameters/ranges/
samplers must yield identical bytes; changing one register, resolved range
offset, sampler float bit or visibility must change bytes; truncated storage,
overflowed count and output alias must fail without reading a guard page or
publishing partial bytes. This catches a real future shader-library/root
compatibility defect. No J313 hardware run can resolve this CPU invariant.

The original CPU encoder `agx_d3d12_r1_root_canonical.h` now implements a
versioned little-endian semantic snapshot with explicit input/output extents,
checked count arithmetic, fieldwise static samplers and exact float bits. It
is included after the current root-private types but not called. An Apple
clang21 ASan/UBSan fixture first failed on the missing header, then returned
`R1_ROOT_CANONICAL_PASS checks=16 failures=0`: source fill/trailing bytes do
not change encoding, range offset and sampler float bits do, short/aliased
output is untouched, and truncated root arrays at a guard page are rejected
before reading. EXP1652 staged six exact source/script SHA256 values and the
pinned WDK26100 ARM64 native-owner/NONE/winsys syntax gates all returned rc=0.
Raw builder log SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
This is a CPU/native-syntax gate only: the current private root does not yet
record its allocation extent or register/retire an owned provider snapshot.
