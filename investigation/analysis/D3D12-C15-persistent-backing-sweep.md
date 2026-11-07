# C15 persistent 64-KiB backing failure during owned-converter cleanup

Classification: host CPU test overlay on pinned, reviewed dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e` composite+profile source;
license/notice conditions remain in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Original reviewed external and current project production sources are unchanged.
A fresh scratch copy changed only the test hook to keep matching
`AGX_C15_BACKING` 64-KiB allocations failing from the selected N until the
current job returns; the caller explicitly disarms before the next job.

WINDOWS CONTRACT: a caught compiler allocation failure must leave the C
owned-converter result empty and must not require a later successful
compiler-site allocation merely to clean up. This host status3 is internal;
it does not establish a permitted WDDM `_0010` `pfnSetErrorCb` mapping.

AGX/ASAHI CONTRACT: the test reaches CPU parser/converter output but no
Mesa NIR, Asahi shader state or GPU queue. KMD, m1n1 and Mu do not own this
TLS allocator cleanup.

TRANSLATION: EXP1725 built nine reviewed static archives/84 objects with
103 C++ exception-enabled commands and the persistent test hook. EXP1726
verified archive/source pins independently. EXP1728 calibrated successful
one-key and mixed-five-key jobs at 3 and 5 matching backing allocations,
then injected each N in a separate child process. Every failure returned
status3 and a byte-zero public result while the hook remained armed through
job return. For all eight N, exactly one matching backing failure occurred;
there was no later 64-KiB backing request during that job's cleanup. After
disarm, the same thread ran a valid job yielding 980/2152 bytes of owned
SPIR-V and 1/5 resource keys. All ten child runs rc0/stderr empty. EXP1729
independently checked each run and archive/source pin:
`.local/c15-composite-persist1725/root-review1729.json`.

WHAT IS STILL UNKNOWN: this says nothing about ordinary new, metadata growth,
huge/no-TLS custom allocations, exception-runtime emergency allocations,
other shader shapes, persistent-failure at other sites or a fully instrumented
83-TU graph. The earlier EXP1724 ordinary-new Nth sweep disarmed after one
failure; its cleanup behavior under persistent compiler-site failure remains
separate. Windows ARM64 `/MD` interposition, signed package, CORE_0010/SM6
and J313 GPU execution remain unvalidated. No new GPU experiment is justified
for this unlinked CPU test.
