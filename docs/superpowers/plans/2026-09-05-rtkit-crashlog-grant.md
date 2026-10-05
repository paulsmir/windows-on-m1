# RTKit crashlog grant — bounded implementation plan

Goal: answer the exact EXP465 endpoint1 request (type1,8192 bytes,DVA0) with
real context0-mapped memory. Keep Windows/HVC/UAT implementation unchanged.

Primary sources: live EXP465 packet and EPMAP0=3/EPMAP1=3; current m1n1
fw/asc/crash.py handle_getbuf (round4KiB request to16KiB and reply allocated
size/DVA), fw/agx/__init__.py AGXASC.ioalloc (context0 UAT mapping); existing
initdata_memory graph and physical-memory owner. Linux rtkit.c corroborates
first crashlog message versus subsequent crash notification. No code copied.

- [ ] Extend the existing graph by one16-KiB object, mapped at driver-owned
  low context0 GPU VA0x430000000 (not a physical address), distinct from current
  buffer-manager VA0x420000000. Existing map validation checks collision/range;
  old kernel virtual addresses must not shift. Existing graph rollback owns it.
- [ ] Register its GPU VA/capacity with the shared RTKit session before CPU
  start. Reject invalid alignment/range/size; never map a firmware-supplied DVA.
- [ ] For the first endpoint1 type1 zero-DVA request within capacity, send
  actual type1/allocated-size/DVA response. Then continue management receiving
  real power acknowledgements. Mark a second crash message as failure, not a
  second allocation. Record grant/request size and last RX using existing style.
- [ ] Tests: low mapping translates to this exact physical object; previous
  initdata/channel addresses unchanged; all allocation failures clean up; exact
  grant bytes; no reply for absent/oversized/preallocated request; second crash
  fails closed. Run existing graph/RTKit/provider/backend suites and WDK.
- [ ] One signed natural candidate with correct broker launcher, exact receipt
  collection and package cleanup. No synthetic power/firmware/completion PASS.

Self-review: one new consumer of proven memory functions, no new allocator,
no active UAT mutation (mapping exists before roots are published), no IRQ or
queue changes. This is within approved firmware implementation authority.
