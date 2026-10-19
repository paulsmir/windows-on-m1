# Native constructor NIR ownership regression

This is an explicit **manual/native regression**, not a default host unittest.
Normal test discovery does not run it, discover tools, access external source
roots or contact another computer. It has no SSH, download, compiler-discovery
or private-preparation fallback. Source selection reads the CURRENT repository
projector relative to this file, not a saved candidate.

The real bug is the initial `agx_uncompiled_shader` allocation returning NULL
without consuming admitted NIR, although later NULL paths consume it. Gallium
`pipe/p_state.h` transfers NIR ownership to the driver. A frontend retaining its
shader must clone it; freeing on every NULL return is incorrect.

The C fixture is the regression: it executes actual selected constructor early
branches with actual native Asahi/pipe types and actual NIR/ralloc. One local
hook returns NULL only for the first exact shader-object `rzalloc` per case.
It preserves NIR's destructor and observes a real ralloc child destructor. It
uses six actual callback assignments, plus two borrowed IR-tag controls; those
controls do not parse or admit TGSI/native programs. No success-path/compiler
stub, pointer-fault test, global allocation hook, BO upload or GPU access occurs.

## Select reproducible actual source

Use the licensed Mesa checkout at commit
`9aa1215f878b504f66159dd2ead4c7973142126e`. Its pristine
`src/gallium/drivers/asahi/agx_state.c` must have SHA-256
`5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba`.
The generated GREEN full-source hash is
`f94f37ebfa8e10d00a219362049878bcd7ab9e96eb0e6397100e94944f0aacac`.
The selector also accepts the explicit `qualified-windows-1469` profile when
that exact already-qualified source is intentionally supplied. There is no
automatic profile/path fallback.

After the implementation and these files are adopted into CURRENT ROOT, run
this DATA-ONLY command with a fresh output directory and the hash of the
CURRENT projector recorded in the experiment inputs:

```text
python tests/nir_constructor_ownership_source.py \
  --state <pinned-Mesa>/src/gallium/drivers/asahi/agx_state.c \
  --profile mesa-9aa1215f-pristine \
  --projector-sha <SHA256-of-CURRENT-drivers/apple-agx/mesa/scripts/build-native-asahi-state.py> \
  --output <fresh-generated-directory>
```

The original selector extracts only the reviewed pure projector helper by AST,
checks both exact guards/rest-of-source/license-prefix proof, and selects each
actual constructor through the COMPLETE first NULL guard. It appends a forbidden
continuation trap and retains the actual six vtable assignment lines. Generated
`red/constructors.inc` and `green/constructors.inc` retain Mesa's full leading
copyright/license notice. They remain external build outputs; no Mesa bodies are
committed. A missing/currently changed helper or input refuses before compilation.

## Build without a fake platform

Use the actual qualified Windows ARM64 LLVM20/native Mesa header configuration,
MSVC14.44.35207 `/MD`, SDK26100 and `NDEBUG`, with the native runtime build's
recorded C flags/include paths. The Asahi headers must be the real prepared
Windows headers, including the canonical unsigned-storage `shader_info.h`.
Do not replace platform/DRM declarations or alter the NIR/ralloc ABI to make a
host build work. This branch test needs the qualified fresh Mesa core libraries;
it does not need the native runtime archive, BO providers, shader compiler or
translator. If a host lacks that header closure, use native build-only plus a
separately approved CPU run instead of inventing shims.

The following PowerShell command pattern uses explicit caller-selected qualified
paths, not discovery. Pin every tool, flags/receipt/header/library/source and
retain raw argv/stdout/stderr/RC before invoking it. Run it on the builder only
under the separately recorded BUILD_ONLY approval. `<fresh-output>` must not
exist. Set the same qualified `INCLUDE`/`LIB` environment as the recorded native
build; remove `CL`/`_CL_` and record their actual absence. Do not change the raw
C profile or turn off NDEBUG.

```powershell
# Explicit inputs, all hash-qualified outside this command pattern:
# $Root = CURRENT repository; $NativeResult = qualified runtime result.json
# $CoreBuild = qualified fresh core build; $Generated = fresh selector output
# $Out = fresh output; $Lld = pinned LLVM20 lld-link.exe
$r = Get-Content -LiteralPath $NativeResult -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $Out | Out-Null
foreach ($variant in @('red','green')) {
  $obj = Join-Path $Out ('consume-' + $variant + '.obj')
  $compile = @($r.compiler.c_flags) + @('/I' + (Join-Path $Generated $variant)) +
    @($r.compiler.includes | ForEach-Object { '/I' + $_ }) +
    @('/showIncludes','/c',(Join-Path $Root 'tests/data/nir_constructor_ownership_fixture.c'),'/Fo' + $obj)
  & $r.compiler.path @compile
  if ($LASTEXITCODE -ne 0) { throw 'COMPILE_STOP' }
  $libraries = @('src/compiler/spirv/libvtn.a','src/compiler/nir/libnir.a',
    'src/compiler/libcompiler.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a',
    'src/util/blake3/libblake3.a','src/c11/impl/libmesa_util_c11.a') |
    ForEach-Object { Join-Path $CoreBuild $_ }
  $link = @('/nologo','/DEBUG','/MACHINE:arm64','/SUBSYSTEM:CONSOLE',
    '/OUT:' + (Join-Path $Out ('consume-' + $variant + '.exe')),
    '/PDB:' + (Join-Path $Out ('consume-' + $variant + '.pdb')),$obj) + $libraries +
    @('synchronization.lib','kernel32.lib','user32.lib','gdi32.lib','winspool.lib',
      'shell32.lib','ole32.lib','oleaut32.lib','uuid.lib','comdlg32.lib','advapi32.lib')
  & $Lld @link
  if ($LASTEXITCODE -ne 0) { throw 'LINK_STOP' }
}
```

Require actual `/showIncludes` to select each matching generated `constructors.inc`
and the one qualified `shader_info.h`, with no pristine-header bypass. Verify
AA64 COFF/PE, imports, reached dependency pins and all input post-hashes. This
command pattern does NOT execute either ARM64 PE on the builder.

## Execute the same invariant for RED and GREEN

On a separately approved native CPU checkpoint, run the two hash-verified EXEs
without arguments, each with a bounded process lifetime and raw byte capture:

```text
consume-red.exe      # expected exit1, six NIR consumption failures
consume-green.exe    # expected exit0, all eight per-case markers PASS
```

The invariant is identical in both: driver consumes NIR exactly once before
returning NULL; borrowed controls remain unconsumed. RED records destruction0,
then the harness explicitly frees the still-owned NIR. GREEN records1 and does
not inspect or even evaluate the freed pointer. Unexpected continuation exits91;
an allocation-hook/shape mismatch exits90. Those are STOPs, not the expected RED.
Do not weaken assertions to obtain a pass. Preserve both old/new results and
count only actual emitted markers. This tests the specific early allocation
boundary, not late failure, successful compilation or general OOM recovery.

Native compile and actual RED/GREEN receipts must be attached by the experiment
owner when qualification completes. This document makes no unexecuted PASS claim.

Qualification: EXP1484 native build-only five commands RC0 with both physical ARM64 PE imports independently reviewed; EXP1487 actual J313 CPU RED PID6548 exit1/32checks6fail and GREEN PID564 exit0/32checks0fail. Both stderr empty, all15 inputs unchanged. Root receipt `.local/c15-nir-consume-cpu1487-result/root-review1487.json` SHA `53e8130d295254c7c9410b6fd94ee1d50d1bab57003eb3affaa3adf7f7919c3c`. This qualifies the exact early branches and fixture; full native constructor and GPU behavior remain outside this test.
