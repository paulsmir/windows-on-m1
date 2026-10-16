"""GPU timestamps: the Mesa projection fills Mesa's timestamp slots.

Invariants caught (docs/superpowers/plans/2026-10-16-gpu-timestamps.md):
- agx_batch_cleanup asks the winsys for the KMD's values after Poll and
  before Release (the capsule that knows the offsets is gone after Release)
  and before it reads the slots;
- a TIMESTAMP End attaches to active and submitted batches (EXP1341:
  Dispatch11's memory barrier submits the compute batch first);
- agx_end_query keeps Asahi's TIMESTAMP path (batches MAX their end in;
  agx_get_gpu_timestamp is the winsys's clock) instead of failing;
- every context owns the per-batch timestamp buffer with a nonzero marker
  handle, and releases it;
- Mesa counts in nanoseconds (the winsys converts the firmware's counter
  ticks), so the conversion is 1/1;
- a compute batch's end lands in comp_end, the slot agx_batch_cleanup reads
  (upstream agx_flush_compute named comp_start for both);
- the screen advertises TIMESTAMP and TIME_ELAPSED queries.
"""
from pathlib import Path
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


class GpuTimestampProjection(unittest.TestCase):
    def test_projection_fills_and_reads_mesa_timestamps(self):
        with tempfile.TemporaryDirectory(prefix="gpu-ts-proj-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            asahi = out / "src/gallium/drivers/asahi"
            batch = (asahi / "agx_batch.c").read_text()
            cleanup = function_body(batch, "agx_batch_cleanup")
            poll = cleanup.index("AgxWin32AsahiBatchPoll(batch, 0)")
            fill = cleanup.index("AgxWin32AsahiBatchTimestamps(batch)")
            release = cleanup.index("AgxWin32AsahiBatchRelease(batch)")
            self.assertLess(poll, fill)
            self.assertLess(fill, release)
            self.assertLess(release, cleanup.index("agx_batch_timestamps(batch)"))
            # EXP1341: a TIMESTAMP also attaches to submitted batches (a
            # Dispatch's barrier flushes its compute batch before the End).
            attach = function_body(batch, "agx_add_timestamp_end_query")
            self.assertIn("foreach_active(ctx, idx)", attach)
            self.assertIn("foreach_submitted(ctx, idx)", attach)
            query = (asahi / "agx_query.c").read_text()
            end = function_body(query, "agx_end_query")
            self.assertIn("agx_add_timestamp_end_query(ctx, query);", end)
            self.assertIn("*value = agx_get_gpu_timestamp(dev);", end)
            self.assertNotIn("timestamp source is not implemented", end)
            pipe = (asahi / "agx_pipe.c").read_text()
            create = function_body(pipe, "agx_create_context")
            self.assertIn("ctx->timestamps = agx_bo_create(agx_device(screen),", create)
            self.assertIn("ctx->timestamp_handle = ctx->timestamps ? 1u : 0u;", create)
            destroy = function_body(pipe, "agx_destroy_context")
            self.assertIn("agx_bo_unreference(dev, ctx->timestamps);", destroy)
            self.assertIn("cmdbuf->ts.end.offset = timestamp_offset(batch, comp_end);", pipe)
            self.assertNotIn("cmdbuf->ts.end.offset = timestamp_offset(batch, comp_start);", pipe)
            self.assertIn("dev.user_timestamp_to_ns.num = 1u;", pipe)
            self.assertIn("dev.user_timestamp_to_ns.den = 1u;", pipe)
            self.assertIn("windows_caps->query_timestamp = true;", pipe)
            self.assertIn("windows_caps->query_time_elapsed = true;", pipe)
            self.assertIn("caps->timer_resolution = agx_gpu_timestamp_to_ns(", pipe)


if __name__ == "__main__":
    unittest.main()
