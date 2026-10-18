# EXP936: verify the existing hardware sharing contract before redirection

WHY THIS HYPOTHESIS: hardwareAGX producer is healthy but kernellegacyBlt returnsPRESENT_OCCLUDED; WARP producer on the same output presentsS_OK. Current native frontend retains software NO_REDIRECTION while implementing some shared-create/open/resolve paths. Do not change that return code based on a callback pointer or one capability probe.

WINDOWS CONTRACT: RENDER_ONLY shared-resource verification before FULL GRAPHICS presentation changes. Create a BGRA2560x1600 DEFAULT texture with SHARED and RT|SRV bindings on the exactAGX adapter. Clear puregreen, wait for an actual D3D11 event query completion, obtain the legacy sharedhandle. A distinct childprocess on the sameconsole/sameadapter opens it, copies to CPU-readable staging and checks all4096000pixels forff00ff00. The producer holds the resource until the child exits. Legacysharedhandles are not passed toCloseHandle. No attempt to parse opaqueDXGI context or forceNO_REDIRECTION/S_OK.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource
https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle
https://learn.microsoft.com/en-us/windows/win32/direct3darticles/surface-sharing-between-windows-graphics-apis

AGX/ASAHI CONTRACT: original9364e7bba96/bdcf/MuR143, queues/power/DART/IRQ/fencesunchanged. Inspected native attach_presentation_render_resource, ScreenAdoptAllocation Direct/split ownership, UMDCreateResource/OpenResource and ResolveSharedResource, Asahi framebuffer batch semantics. Windows/KMD owns allocationsharing; eachUMD owns its own GPUVA mapping/native BO references; actual rendercompletion is required before consumer access. No external implementation copied.

TRANSLATION: diagnostic application plus its child only. One producer/consumer resource lifecycle is the indivisible sharing invariant.2s event-query budget,15s childbudget; outerobserver bounded30s and terminates only ownedprocesses ifrequired. A shared-open or readbackfailure localizes a real missingcontract; it is not proof of a display/DARTfault.

WHAT IS STILL UNKNOWN: whether currently admitted sharedBGRA resources genuinely survive cross-process open, preserve metadata/lifetime and expose the rendered pixels. Existing mocks cannot establish that. A successful probe is necessary evidence for currentD3D10/11 sharing, not automaticproof ofD3D9 interoperability orpermission to change allpresentationcaps. MicrosoftROS reference is RENDER_ONLY; its10_2-only exports andS_OK demonstrate commonCreateDevice/shared-path usage but do not supply fullgraphics admission/scheduler assumptions.

Verification: SDK26100/v14314.44 ARM64/W4WX/PREfast, source/binaryhash+PE. No manufacturedRED test for this measurement-only probe. Preserve936originalboot and immutable377/392 recovery. Build andrun preregistered, no packageinstallation/reboot.
