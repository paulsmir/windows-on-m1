"""Exercise m1n1's actual AArch64 load decoder for the R64 MMIO value."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1] / "m1n1_windows/src/hv_vm.c"


class HvVmLoadDecodeTest(unittest.TestCase):
    def test_ldr_x_preserves_64_bits_and_ldrsw_sign_extends_32_bits(self):
        source = SOURCE.read_text()
        body = source.split("union simd_reg {", 1)[1].split("static bool emulate_store", 1)[0]
        harness = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
struct exc_info { u64 regs[32]; };
#define BIT(n) (UINT64_C(1) << (n))
#define CHECK_RN if (Rn == 31) return false
#define DECODE_OK if (!val) return true
#define EXT(n,b) (((int32_t)(((uint32_t)(n)) << (32 - (b)))) >> (32 - (b)))
#define dprintf(...) ((void)0)
union simd_reg {
""" + body
        harness = harness.replace(
            "static bool emulate_load",
            "static void get_simd_state(union simd_reg *p) { (void)p; }\n"
            "static void put_simd_state(union simd_reg *p) { (void)p; }\n"
            "static bool emulate_load",
            1,
        )
        harness += r"""
int main(void) {
    struct exc_info ctx = {0};
    u64 width = 0, addr = 0, value = UINT64_C(0x8e0000000);
    /* LDR X0, [X1]: the complete guest IPA reaches Xt. */
    assert(emulate_load(&ctx, UINT32_C(0xf9400020), NULL, &width, &addr));
    assert(width == 3);
    assert(emulate_load(&ctx, UINT32_C(0xf9400020), &value, &width, &addr));
    assert(ctx.regs[0] == value);
    /* LDRSW X0, [X1]: only a signed DWORD can yield the Code12 pattern. */
    value = UINT32_C(0xe0000000);
    assert(emulate_load(&ctx, UINT32_C(0xb9800020), NULL, &width, &addr));
    assert(width == 2);
    assert(emulate_load(&ctx, UINT32_C(0xb9800020), &value, &width, &addr));
    assert(ctx.regs[0] == UINT64_C(0xffffffffe0000000));
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "decode.c"
            binary = Path(directory) / "decode"
            path.write_text(harness)
            build = subprocess.run(
                [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(path), "-o", str(binary)], capture_output=True, text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
