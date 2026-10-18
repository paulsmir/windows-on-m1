# Local inference health and authority

Model: `devstral-small-2:24b-instruct-2512-q4_K_M`.
Expected digest: `24277f07f62db8f9cb68e9dfc679ea1818a7fbac47a50eff0a701d3f645b63c8`.
Builder: FRYZZING / RX7900XT20GB. Local worker shell authority: NONE.

Before a task, verify one intended model and num_ctx4096 in `/api/ps`.
Require size_vram>0 and GPU residency ratio >=0.95 (essentially fully resident).
Never send a coding/review task on a failed check. Missing or malformed telemetry
is failure. Client writes LOCAL_WORKER_BLOCKED / GPU_RESIDENCY_FAILED.
Use a single host-side exclusive lock for client requests; worker and reviewer
have separate message arrays and execute sequentially. Keep model loaded10m
through an active task/review sequence; context remains4096. Postflight rechecks
residency before accepting a response. No automatic model/quantization fallback.

Tiny warm-only request: one predicted token, num_ctx4096; allowed solely to
measure residency. On failure cancel/unload, inspect conflicting runtime and
device availability, then perform only bounded recovery. No substantive CPU run.

## Recovery performed 2026-09-12

The old loopback API was Ollama0.21.2 in Ubuntu WSL, service ollama PID4229;
it loaded Devstral with size_vram0 and reported100%CPU. Windows Ollama0.34.0
was simultaneously unable to bind11434. Its logs repeatedly reported port busy.
This explained why the native AMD runtime was not handling the requests.

Cancelled current local request, unloaded the old model, stopped only WSL
ollama.service (did NOT disable it or stop Ubuntu/other services), and stopped
the conflicting native Ollama app process. Started installed Windows Ollama
on127.0.0.1:11434 with process-local context4096/parallel1/keepalive10m.
No persistent environment or model changes. No GPU-driver changes.
Native runtime detected AMD RX7900XT using ROCm gfx1100,20GiB total/19.8GiB
available. Tiny warm request then produced:

```
SIZE_TOTAL=14919579729
SIZE_VRAM=14919579729
GPU_RESIDENCY_RATIO=1.0
NUM_CTX=4096
GPU_PREFLIGHT=PASS
```

Loopback SSH tunnel uses strict host checking and Windows builder key. The
runtime and tunnel are controlled processes; do not start duplicates. Service
restoration, if explicitly needed later, is WSL `systemctl start ollama` after
stopping native runtime, not concurrent startup on the same port. WSL service
enabled state remains unchanged. Keep native GPU execution for this workflow.

First GPU proposal took4.23s; separate reviewer2.99s. CPU-only failed requests
remain in `.local/phase3/demo-proposal-001` and `demo-proposal-002`; they are
not successful worker evidence. No further CPU task requests are permitted.
