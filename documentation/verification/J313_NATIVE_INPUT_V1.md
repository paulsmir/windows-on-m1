# J313 native input v1 checkpoint

`j313-native-input-v1` is the permanent return point for the first fully usable
built-in input stack on the 2020 M1 MacBook Air. It is an input checkpoint, not
a claim that GPU acceleration, Firestorm CPUs, sleep, audio, or the rest of the
platform is complete.

## Validated behavior

The operator confirmed all of the following in the same live Windows session:

- built-in keyboard typing;
- built-in trackpad pointer motion;
- left click and right click;
- multitouch input;
- simultaneous keyboard and trackpad use;
- responsive, stable Windows operation during the bounded qualification.

The final metadata-only driver snapshot recorded 28/28 accepted and submitted
keyboard reports and 7185/7185 decoded and submitted trackpad reports. Both VHF
frontends remained running, Windows feature negotiation succeeded, and every
recorded transport, parser, feature, and submission error counter was zero.
The full hardware record is `EXP-20260824-054`.

## Exact source and artifacts

| Component | Revision or SHA-256 |
| --- | --- |
| Validated root runtime source | `f6a90f6acabb8e057f93d44cb07cfb2113fc007c` |
| m1n1 fork | `2fe790beebed32658eae753dee3e6d581df97197` |
| Mu fork | `9501de460353b902dbbd3b7de42c703af811f037` |
| WDK workflow run | `32754271477` |
| `AppleInput.sys` | `7b75873de00a392b6e906edf5776f69c274e86814fb02389414ef557d2b7bdb5` |
| `AppleInput.inf` | `ca844ebf9a0fab6ae4a6aa434033eb487ca246b9248bc4fde968539ca26565cd` |
| `appleinput.cat` | `e11befe19ef7b0dac31360b348394a65259dcb12ea7e7b6bd8ca66097dc0187f` |
| `AppleInputDiag.exe` | `d842e47ee5b8c9299b3f3ceb8027855c016f28494fb4e0f4be7dc0d801f5c3f7` |
| J313 assisted EFI | `cd591aab2ef0641902f03e1a38aac697e45fc12e466a3cb72f32cd3d68060710` |
| assisted m1n1 Mach-O | `e4c073c28d2d008aa0159cf3e64f5daa2afabe0bb712b68198ea8d917381a3a6` |
| physical diagnostic snapshot | `1b87c25e4294b2ccc7083c80648e914bfd3c7c90d6ed2fd81078dce7c7ba0c71` |

The validated Windows installation assigned the package `oem16.inf`; that name
is machine-local and must not be hard-coded on another installation. Its service
parameters were:

```text
TransportOnly=0
PublishKeyboard=1
PublishTrackpad=1
```

Windows test signing and the exact catalog signer certificate were enabled.
The package build, certificate trust, staged installation, diagnostic gates,
and rollback procedure are in `documentation/APPLE_INPUT.md`.

## Return to this source checkpoint

Preserve current work on another branch first. Then restore the public root:

```sh
git fetch origin --tags
git switch --detach j313-native-input-v1
```

The root repository does not silently select arbitrary nested checkouts. Verify
and restore the exact platform forks explicitly:

```sh
git -C m1n1_windows fetch origin
git -C m1n1_windows switch --detach 2fe790beebed32658eae753dee3e6d581df97197
git -C mu fetch origin
git -C mu switch --detach 9501de460353b902dbbd3b7de42c703af811f037
```

Rebuild the platform from those revisions and install only a driver package
whose four hashes match this file. If the exact CI artifact is unavailable,
rebuild from the tag, record the new hashes, and repeat the staged hardware
gate instead of assuming binary identity.

To resume normal development after inspecting the checkpoint:

```sh
git switch feature/j313-native-input
```

CPU work must occur on a new branch created after this tag. Do not rewrite or
move `j313-native-input-v1`; later improvements get new commits and new tags.
