TASK_ID:
AD04-AGXINDEX-LAYOUT-003

INPUT_COMMIT:
d7be0d8085106d56b4fb5d13fc38473f70067ba3

RESULT:
PASS for homogeneous-unsigned agx_index discriminator; stop at first new compiler blocker.

ORIGINAL_WINDOWS_SIZE:
20

CANDIDATE_X64_SIZE:
8

CANDIDATE_X64_ALIGN:
4

CANDIDATE_ARM64_SIZE:
8 (compile-only static assertion)

ENUM_SEMANTICS:
PASS for size values 0..2 and type values including REGISTER=4 and UNDEF=5.

BOOL_SEMANTICS:
PASS; six modifier bits plus has_reg preserve 0/1 values.

FORMAL_TYPE_DEPENDENCIES_FOUND:
NO. Audit found ordinary reads/writes/comparisons only; no _Generic/typeof,
field address-taking or field-type dispatch. Raw object uses are intentional.

NATIVE_ORACLE_CASES:
256 deterministic states covering zero, values, all booleans, channels 0..7,
size 0..2, type 0..5, register boundary values 0..2047 and has_reg.

RAW_BYTE_MATCH:
256/256

RAW_BYTE_EQUIVALENCE:
PASS; native and Windows candidate corpus SHA256 both
1945993a899484ad97f1095362f3ca13c455cac9da2943776e125def3bffee05.

STATIC_ASSERT:
PASS unchanged (`sizeof(agx_index) == 8`).

CROSS_TU_CONSISTENCY:
Derived staging copies the pinned Asahi compiler translation-unit set and
replaces only agx_compiler.h; exact agx_compile.c control resolves the same
derived header. Cross-TU user list is preserved in evidence.

PINNED_SOURCE_CHANGED:
NO

OVERLAY_TRANSFORMATION:
Hash-pinned deterministic generator changes exactly nine formal types inside
agx_index: bool metadata fields and enum size/type become unsigned storage;
names, widths, order, constructors and static_assert remain unchanged.

GLOBAL_MNO_MS_BITFIELDS_USED:
NO

FRESH_COMPILER_RESULT:
Exact x64 agx_compile.c control with u_atomic overlay, fourcc compatibility
input and derived homogeneous agx_index header no longer reports size20/assertion.

FIRST_NEW_BLOCKER:
off_t unknown type name at agx_compiler.h line478. Later UTIL_LUT2 diagnostics
remain visible and were not modified.

WORKER_COMMIT:
PENDING (filled after implementation commit)

HARDWARE_USED:
NO

CONTRACT_VIOLATIONS:
NONE

RAW_EVIDENCE_PATHS:
investigation/evidence/AD04-agxindex-layout-003/
