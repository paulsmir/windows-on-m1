# Guarded Unattended Windows Reinstall Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a hash-pinned WinPE workflow that safely redeploys Windows 11 Pro ARM64 into the existing J313 Windows volumes, creates the `pavel` account without interactive OOBE, configures French AZERTY, installs the qualified AppleInput package, and provisions host-key SSH access.

**Architecture:** Extend the existing volume-scoped `reinstall-windows.cmd`; never select, clean, delete, or repartition a physical disk. Keep machine-independent configuration in an ARM64 unattend file, perform test-signed AppleInput and OpenSSH setup from an idempotent first-boot PowerShell script, and use a macOS-side builder to verify/copy payloads and inject an automatic launcher into both WinPE WIM indices.

**Tech Stack:** WinPE `cmd.exe`, DiskPart, DISM, BCDBoot, Windows unattended setup XML, PowerShell 5.1, PnPUtil, CertUtil, OpenSSH, POSIX shell, `wimlib-imagex`, Python `unittest`/XML parsing.

**Spec:** `docs/superpowers/specs/2026-09-02-guarded-unattended-windows-reinstall-design.md`

## Global Constraints

- Format only the unique existing NTFS `Windows` volume and FAT32 `WINESP` volume; never mutate the GPT or Apple/Asahi partitions.
- Require exact confirmation `ERASE WINDOWS` after all read-only target and evidence-preservation gates.
- Install Windows 11 Pro ARM64 image index 3 from build 26200.8037 media.
- Configure English `en-GB` UI, French AZERTY `040C:0000040C`, `Romance Standard Time`, computer `J313-WIN`, and local administrator `pavel` with password `a`.
- Provision only `/Users/pavel/.ssh/air.pub`; never copy `/Users/pavel/.ssh/air`.
- Disable SSH password authentication; retain password `a` for local console recovery only.
- Install only the hash-pinned EXP145 AppleInput package and set `TransportOnly/PublishKeyboard/PublishTrackpad` to `0/1/1`.
- Leave Windows inbox USBXHCI/HID unchanged and leave AppleAgx entirely absent.
- Do not modify the active USB media until all host-side tests pass and its target mount is revalidated.
- Do not perform the destructive reinstall without a separate exact hardware authorization and preregistered experiment.

---

## File structure

- `scripts/reinstall-windows.cmd`: WinPE discovery, same-disk proof, evidence preservation, destructive gate, image deployment, offline configuration, BCDBoot, and receipts.
- `scripts/reinstall/winpeshl.ini`: automatic WinPE entry point.
- `scripts/reinstall/unattend-arm64.xml`: specialize/OOBE contract for locale and local account.
- `scripts/reinstall/SetupComplete.cmd`: one-purpose bridge that launches first-boot provisioning under SYSTEM and records its exit code.
- `scripts/reinstall/configure-first-boot.ps1`: idempotent AppleInput, SSH, Fast Startup, autologon cleanup, and baseline receipt logic.
- `scripts/prepare-reinstall-media.sh`: host-side media identity, payload/hash, backup, WIM injection, and final verification.
- `tests/test_reinstall_windows.py`: deterministic WinPE destructive-order and target-safety contracts.
- `tests/test_reinstall_unattend.py`: XML settings and secret/architecture contracts.
- `tests/test_reinstall_first_boot.py`: PowerShell security, driver identity, and idempotence contracts.
- `tests/test_prepare_reinstall_media.py`: host builder source/target and WIM safety contracts.
- `documentation/INSTALL.md`: operator instructions and recovery behavior.

### Task 1: Strengthen WinPE target identity and preservation gates

**Files:**
- Modify: `scripts/reinstall-windows.cmd`
- Modify: `tests/test_reinstall_windows.py`

**Interfaces:**
- Consumes: exact source marker `sources\\install.wim`, target labels `Windows` and `WINESP`, WinPE `diskpart`, `dism`, `certutil`, and source-volume free space.
- Produces: environment variables `SOURCE_DRIVE`, `WINDOWS_DRIVE`, `WINESP_DRIVE`, `TARGET_DISK`, and `EVIDENCE_ROOT`; a durable `pre-reinstall` manifest; exit codes before any format on ambiguity.

- [ ] **Step 1: Add failing target-safety tests**

Add assertions that the production script emits and checks DiskPart `detail volume` records for both targets, requires a single common disk, excludes the source volume, preserves evidence before confirmation, and never emits disk-level destructive commands:

```python
def test_proves_both_targets_share_one_non_source_disk(self):
    for token in (
        "detail volume",
        "target_disk",
        "windows_target_disk",
        "winesp_target_disk",
        "source_target_disk",
    ):
        self.assertIn(token, self.lower)
    self.assertIn(
        'if not "!windows_target_disk!"=="!winesp_target_disk!"',
        self.lower,
    )
    self.assertIn(
        'if "!source_target_disk!"=="!target_disk!"',
        self.lower,
    )

def test_preserves_forensic_set_before_confirmation_and_format(self):
    preserve = self.lower.index("phase: preserve pre-reinstall evidence")
    confirmation = self.lower.index('set /p "confirm=')
    first_format = self.lower.index("format fs=ntfs")
    self.assertLess(preserve, confirmation)
    self.assertLess(confirmation, first_format)
    for item in ("system", "software", "default", "sam", "security", "bcd"):
        self.assertIn(item, self.lower)

def test_never_constructs_disk_destructive_commands(self):
    for forbidden in ("clean", "delete partition", "create partition", "convert gpt"):
        self.assertNotRegex(self.lower, rf"(?im)^\s*{forbidden}\b")
```

- [ ] **Step 2: Run the focused tests and confirm RED**

Run: `python3 -m unittest tests.test_reinstall_windows -v`

Expected: the new same-disk/preservation assertions fail; all pre-existing tests remain green.

- [ ] **Step 3: Implement read-only disk relationship proof**

Generate one temporary DiskPart script per resolved volume:

```cmd
>"!DETAIL_WINDOWS_DISKPART!" echo select volume !WINDOWS_VOLUME!
>>"!DETAIL_WINDOWS_DISKPART!" echo detail volume
>"!DETAIL_WINESP_DISKPART!" echo select volume !WINESP_VOLUME!
>>"!DETAIL_WINESP_DISKPART!" echo detail volume
```

Parse only lines beginning with `Disk ###`; require one disk number from each target and equality. Resolve the source volume's disk in the same way and reject equality with `TARGET_DISK`. Keep `select disk`, `clean`, partition creation, deletion, and conversion absent from generated scripts.

- [ ] **Step 4: Implement bounded pre-format evidence preservation**

Create `SOURCE_DRIVE:\J313-Reinstall-Evidence\<date>-<time>` and copy, when present, the five base hives plus LOG1/LOG2, the ESP BCD, minidumps, `MEMORY.DMP` metadata, `EXP305*`, and `EXP316-pre-vss-restore`. Use `robocopy /COPY:DAT /DCOPY:T /R:1 /W:1` only for directories and plain `copy /b` for fixed files. Generate hashes with:

```cmd
for /r "!EVIDENCE_ROOT!" %%F in (*) do certutil -hashfile "%%F" SHA256 >>"!EVIDENCE_ROOT!\SHA256.txt"
```

Require the manifest, current hives, and BCD copy to exist before showing the destructive confirmation. Treat optional dump/EXP directories as recorded `ABSENT`, not failures.

- [ ] **Step 5: Run focused and regression tests**

Run: `python3 -m unittest tests.test_reinstall_windows -v`

Expected: PASS with no `clean`, partition creation/deletion, or hard-coded drive letters.

- [ ] **Step 6: Commit Task 1**

```bash
git add scripts/reinstall-windows.cmd tests/test_reinstall_windows.py
git commit -m "feat(installer): prove targets and preserve recovery evidence"
```

### Task 2: Add validated ARM64 unattended configuration

**Files:**
- Create: `scripts/reinstall/unattend-arm64.xml`
- Create: `tests/test_reinstall_unattend.py`

**Interfaces:**
- Consumes: Windows 11 ARM64 image index 3 and password/account settings from the approved spec.
- Produces: a parseable answer file copied as `Windows\\Panther\\unattend.xml` by Task 4.

- [ ] **Step 1: Write failing XML contract tests**

Create tests using `xml.etree.ElementTree` and local-name lookup:

```python
ROOT = Path(__file__).resolve().parents[1]
ANSWER = ROOT / "scripts/reinstall/unattend-arm64.xml"

def values(name):
    tree = ET.parse(ANSWER)
    return [node.text for node in tree.iter() if node.tag.rsplit("}", 1)[-1] == name]

class UnattendTests(unittest.TestCase):
    def test_arm64_and_locale_contract(self):
        text = ANSWER.read_text(encoding="utf-8")
        self.assertNotIn("processorArchitecture=\"amd64\"", text)
        self.assertIn("processorArchitecture=\"arm64\"", text)
        self.assertEqual(values("InputLocale"), ["040C:0000040C"] * 2)
        self.assertIn("en-GB", values("UILanguage"))
        self.assertIn("Romance Standard Time", values("TimeZone"))

    def test_local_account_and_oobe_contract(self):
        self.assertIn("pavel", values("Name"))
        self.assertIn("Administrators", values("Group"))
        self.assertIn("a", values("Value"))
        self.assertIn("true", [v.lower() for v in values("HideOnlineAccountScreens")])
```

- [ ] **Step 2: Run the tests and confirm RED**

Run: `python3 -m unittest tests.test_reinstall_unattend -v`

Expected: FAIL because the answer file does not exist.

- [ ] **Step 3: Create the answer file**

Use `urn:schemas-microsoft-com:unattend`, ARM64 components, and these exact settings:

```xml
<settings pass="specialize">
  <component name="Microsoft-Windows-International-Core"
             processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35"
             language="neutral" versionScope="nonSxS">
    <InputLocale>040C:0000040C</InputLocale>
    <SystemLocale>en-GB</SystemLocale>
    <UILanguage>en-GB</UILanguage>
    <UserLocale>en-GB</UserLocale>
  </component>
  <component name="Microsoft-Windows-Shell-Setup"
             processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35"
             language="neutral" versionScope="nonSxS">
    <ComputerName>J313-WIN</ComputerName>
    <TimeZone>Romance Standard Time</TimeZone>
  </component>
</settings>
```

In `oobeSystem`, repeat the international settings, create `pavel` in `Administrators` with plaintext value `a`, configure AutoLogon with `LogonCount=1`, suppress supported OOBE account/network/privacy pages, and add a first-logon command that clears `AutoLogonCount` exactly as documented by Microsoft.

- [ ] **Step 4: Run tests and XML parser**

Run: `python3 -m unittest tests.test_reinstall_unattend -v`

Expected: PASS; the file parses as XML and contains no `amd64` component.

- [ ] **Step 5: Commit Task 2**

```bash
git add scripts/reinstall/unattend-arm64.xml tests/test_reinstall_unattend.py
git commit -m "feat(installer): add ARM64 unattended baseline"
```

### Task 3: Add idempotent AppleInput and SSH first-boot provisioning

**Files:**
- Create: `scripts/reinstall/SetupComplete.cmd`
- Create: `scripts/reinstall/configure-first-boot.ps1`
- Create: `tests/test_reinstall_first_boot.py`

**Interfaces:**
- Consumes: `C:\J313-Reinstall\AppleInput\AppleInput.inf`, `.cat`, `.sys`, `.cer`, `air.pub`, and `payload.sha256` copied by Task 4.
- Produces: `C:\J313-Reinstall\first-boot-result.json`, healthy AppleInput `0/1/1`, automatic `sshd`, administrative key login, disabled password login/Fast Startup, and an idempotent scheduled retry if OpenSSH payload is unavailable.

- [ ] **Step 1: Write failing provisioning security tests**

```python
class FirstBootTests(unittest.TestCase):
    def setUp(self):
        self.ps1 = (ROOT / "scripts/reinstall/configure-first-boot.ps1").read_text()
        self.lower = self.ps1.lower()

    def test_never_contains_or_copies_private_key(self):
        self.assertNotIn("/users/pavel/.ssh/air\n", self.lower)
        self.assertIn("air.pub", self.lower)
        self.assertIn("administrators_authorized_keys", self.lower)

    def test_disables_ssh_password_authentication(self):
        self.assertIn("passwordauthentication no", self.lower)
        self.assertIn("restart-service sshd", self.lower)

    def test_pins_appleinput_and_sets_vhf_contract(self):
        for digest in EXPECTED_APPLEINPUT_HASHES:
            self.assertIn(digest, self.lower)
        for name, value in (("transportonly", "0"), ("publishkeyboard", "1"), ("publishtrackpad", "1")):
            self.assertRegex(self.lower, rf"{name}.*{value}")
        self.assertNotIn("appleagx", self.lower)
```

- [ ] **Step 2: Run the tests and confirm RED**

Run: `python3 -m unittest tests.test_reinstall_first_boot -v`

Expected: FAIL because both provisioning files are absent.

- [ ] **Step 3: Implement the SYSTEM bridge**

`SetupComplete.cmd` invokes:

```cmd
@echo off
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File C:\J313-Reinstall\configure-first-boot.ps1
set "RC=%ERRORLEVEL%"
>C:\J313-Reinstall\setupcomplete-exit.txt echo %RC%
exit /b 0
```

The bridge always returns zero so Windows setup can reach the desktop; the real status remains durable in `setupcomplete-exit.txt` and JSON.

- [ ] **Step 4: Implement hash-pinned AppleInput setup**

The PowerShell script defines exact expected hashes, compares each with `Get-FileHash`, imports only the pinned certificate, runs `pnputil /add-driver ... /install`, discovers the published INF through `DEVPKEY_Device_DriverInfPath`, sets the three service values with `New-ItemProperty`, and restarts only `ACPI\APPL0001\0` if present. Any mismatch skips installation and records failure; it never guesses `oemNN.inf`.

- [ ] **Step 5: Implement public-key OpenSSH setup**

Use `Get-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0`; install it only when `NotPresent`, enable/start `sshd`, write the exact single-line `air.pub` to `%ProgramData%\ssh\administrators_authorized_keys`, apply SYSTEM/Administrators-only ACLs with `icacls`, ensure `sshd_config` contains one effective `PasswordAuthentication no`, enable only the built-in OpenSSH firewall rule, and restart `sshd`.

If capability installation reports source/network unavailability, register a SYSTEM scheduled task named `J313-FinishProvisioning` with a five-minute startup/network retry. On success, the script deletes that task.

- [ ] **Step 6: Implement baseline settings and JSON receipt**

Run `powercfg.exe /hibernate off`, set `HiberbootEnabled=0`, clear Winlogon autologon values, collect CPU count, services, APPL0001/APPL0002 status, AppleInput driver INF/hash, VHF children, and recent Event 129/BugCheck records, then serialize to `first-boot-result.json` with `ConvertTo-Json -Depth 8`.

- [ ] **Step 7: Run focused tests**

Run: `python3 -m unittest tests.test_reinstall_first_boot -v`

Expected: PASS; private-key path and AppleAgx are absent.

- [ ] **Step 8: Commit Task 3**

```bash
git add scripts/reinstall/SetupComplete.cmd scripts/reinstall/configure-first-boot.ps1 tests/test_reinstall_first_boot.py
git commit -m "feat(installer): provision input and SSH on first boot"
```

### Task 4: Integrate offline configuration into WinPE deployment

**Files:**
- Modify: `scripts/reinstall-windows.cmd`
- Modify: `tests/test_reinstall_windows.py`

**Interfaces:**
- Consumes: Task 2 answer file and Task 3 provisioning payload from the WinPE filesystem at `X:\J313-Reinstall`.
- Produces: a newly applied Windows tree containing `Windows\Panther\unattend.xml`, `Windows\Setup\Scripts\SetupComplete.cmd`, `C:\J313-Reinstall`, French input defaults, test-signing BCD, and verified ARM64 fallback loader.

- [ ] **Step 1: Add failing integration-order tests**

Assert this strict order:

```python
apply_image = self.lower.index("dism /apply-image")
locale = self.lower.index("/set-inputlocale:040c:0000040c")
panther = self.lower.index(r"\windows\panther\unattend.xml")
setupcomplete = self.lower.index(r"\windows\setup\scripts\setupcomplete.cmd")
bcdboot = self.lower.index("bcdboot")
testsigning = self.lower.index("testsigning on")
self.assertEqual(
    [apply_image, locale, panther, setupcomplete, bcdboot, testsigning],
    sorted([apply_image, locale, panther, setupcomplete, bcdboot, testsigning]),
)
```

Also require hash verification of all payload files before copy and forbid every `AppleAgx` token.

- [ ] **Step 2: Run focused tests and confirm RED**

Run: `python3 -m unittest tests.test_reinstall_windows -v`

Expected: FAIL on missing locale/Panther/provisioning phases.

- [ ] **Step 3: Add offline locale and payload phases**

After successful image application, run:

```cmd
dism /Image:"!WINDOWS_DRIVE!\" /Set-InputLocale:040C:0000040C /Set-SysLocale:en-GB /Set-UserLocale:en-GB /Set-TimeZone:"Romance Standard Time"
```

Create the Panther, Setup Scripts, and `J313-Reinstall` directories; copy the answer file, bridge, PowerShell, public key, manifest, and exact AppleInput directory with checked exit codes and post-copy SHA-256 validation.

- [ ] **Step 4: Build fresh BCD and set only required values**

Run BCDBoot with `/l en-GB /s <WINESP> /f UEFI /v`, copy the ARM64 fallback loader, and set `testsigning on` on the new default loader using the explicit offline store. Do not copy any old BCD value, resume object, debug setting, or recovery override.

- [ ] **Step 5: Expand final artifact verification**

Require the answer file, SetupComplete bridge, PowerShell script, public key, AppleInput payload, `winload.efi`, BCD, and `BOOTAA64.EFI`. Write their hashes plus final DiskPart inventory to the USB log directory.

- [ ] **Step 6: Run focused tests**

Run: `python3 -m unittest tests.test_reinstall_windows tests.test_reinstall_unattend tests.test_reinstall_first_boot -v`

Expected: PASS.

- [ ] **Step 7: Commit Task 4**

```bash
git add scripts/reinstall-windows.cmd tests/test_reinstall_windows.py
git commit -m "feat(installer): integrate unattended offline deployment"
```

### Task 5: Build a guarded macOS media preparer

**Files:**
- Create: `scripts/reinstall/winpeshl.ini`
- Create: `scripts/prepare-reinstall-media.sh`
- Create: `tests/test_prepare_reinstall_media.py`

**Interfaces:**
- Consumes: mounted source ISO, mounted exFAT USB, `wimlib-imagex`, repository payload, exact EXP145 package, and `/Users/pavel/.ssh/air.pub`.
- Produces: backed-up and modified USB `boot.wim`, root recovery launcher, `J313-Reinstall/payload.sha256`, and `media-manifest.txt`; never accepts or copies a private key.

- [ ] **Step 1: Write failing builder contract tests**

```python
class MediaBuilderTests(unittest.TestCase):
    def test_requires_exact_mount_and_arm64_media(self):
        for token in ("--media", "bootaa64.efi", "install.wim", "architecture:           arm64"):
            self.assertIn(token, self.lower)

    def test_backs_up_boot_wim_before_update(self):
        backup = self.lower.index("boot.wim.original")
        update = self.lower.index("wimlib-imagex update")
        self.assertLess(backup, update)
        self.assertIn("wimlib-imagex verify", self.lower)

    def test_copies_public_key_only(self):
        self.assertIn("air.pub", self.lower)
        self.assertNotRegex(self.lower, r"cp .*\.ssh/air(?:\s|\")")
```

- [ ] **Step 2: Run tests and confirm RED**

Run: `python3 -m unittest tests.test_prepare_reinstall_media -v`

Expected: FAIL because the builder and launcher do not exist.

- [ ] **Step 3: Add the automatic WinPE launcher**

```ini
[LaunchApps]
%SYSTEMROOT%\System32\wpeinit.exe
%SYSTEMROOT%\System32\cmd.exe, "/k X:\J313-Reinstall\reinstall-windows.cmd"
```

The installer still stops before mutation until target proof, evidence preservation, and literal confirmation complete.

- [ ] **Step 4: Implement host-side validation and staging**

The shell script must use `set -eu`, accept only `--media /absolute/mount`, resolve it with `cd "$media" && pwd -P`, require a writable exFAT mount, verify AArch64 `BOOTAA64.EFI`, inspect `install.wim` with `wimlib-imagex info`, require index 3 to be `Windows 11 Pro`/ARM64/build 26200, and require at least 12 GiB free.

Copy the exact AppleInput package and public key to a temporary payload directory, verify their four approved hashes and SSH fingerprint `SHA256:pzT35zYDdvVfzT6yC9rGt3WPATApWtH2yoemlH2Rbmg`, then generate SHA-256 records. Reject any private-key file in the payload tree.

- [ ] **Step 5: Implement recoverable WIM update**

Copy `sources/boot.wim` to `sources/boot.wim.original` before the first update. Generate a wimlib command file containing exact `add` paths for the payload and `winpeshl.ini`, update both image indices, and run `wimlib-imagex verify`. On any failure, restore `boot.wim.original`. On success, retain the original until the first successful installer boot.

- [ ] **Step 6: Run builder tests**

Run: `python3 -m unittest tests.test_prepare_reinstall_media -v`

Expected: PASS.

- [ ] **Step 7: Commit Task 5**

```bash
git add scripts/reinstall/winpeshl.ini scripts/prepare-reinstall-media.sh tests/test_prepare_reinstall_media.py
git commit -m "feat(installer): build recoverable unattended WinPE media"
```

### Task 6: Documentation, complete software verification, and USB build

**Files:**
- Modify: `documentation/INSTALL.md`
- Modify: `investigation/CHANGES.csv`
- Modify: `investigation/EXPERIMENTS.md` only to mark unlaunched EXP334 superseded and preregister the later installer media qualification; do not rewrite old verdicts.

**Interfaces:**
- Consumes: all Task 1–5 artifacts and the mounted `/Volumes/WINDOWS ARM` USB.
- Produces: reviewed documentation, green test suite, committed change-ledger rows, and a hash-pinned installer USB ready but not yet authorized to erase Windows.

- [ ] **Step 1: Update operator documentation**

Document the exact host command:

```sh
scripts/prepare-reinstall-media.sh --media "/Volumes/WINDOWS ARM"
```

Document automatic WinPE launch, the displayed target proof, evidence directory, exact `ERASE WINDOWS` gate, index 3, `pavel`/local password behavior, French keyboard, AppleInput, SSH-key access, failure recovery using `boot.wim.original`, and the fact that AppleAgx remains absent.

- [ ] **Step 2: Run focused and public regression tests**

Run:

```bash
python3 -m unittest \
  tests.test_reinstall_windows \
  tests.test_reinstall_unattend \
  tests.test_reinstall_first_boot \
  tests.test_prepare_reinstall_media -v
python3 -m pytest tests/test_public_documentation.py tests/test_change_ledger.py -q
```

Expected: all tests PASS.

- [ ] **Step 3: Build a temporary media clone first**

Copy the USB's `boot.wim` and required root boot files to a temporary exFAT/FAT image or temporary directory accepted by the builder test harness. Run the builder there, verify both WIM indices contain the exact payload, and verify the post-build WIM. Do not modify `/Volumes/WINDOWS ARM` during this step.

- [ ] **Step 4: Commit implementation and ledger records**

Append RFC 4180 `CHANGES.csv` rows using each implementation commit hash, `status=implemented`, software test evidence, and no hardware-validation claim. Commit documentation/ledger bookkeeping separately.

- [ ] **Step 5: Revalidate the live USB and build it**

Recheck that `/Volumes/WINDOWS ARM` is the 29 GiB exFAT `/dev/disk8s1`, source ISO remains read-only, required WIM sizes/identity match, and no internal volume is selected. Run the builder once. Verify:

```bash
wimlib-imagex verify "/Volumes/WINDOWS ARM/sources/boot.wim"
wimlib-imagex dir "/Volumes/WINDOWS ARM/sources/boot.wim" 1 /J313-Reinstall
wimlib-imagex dir "/Volumes/WINDOWS ARM/sources/boot.wim" 2 /J313-Reinstall
```

Record the final boot/install WIM hashes, payload hashes, media mount/device, and build time. At this point the USB is ready, but installed Windows is unchanged.

- [ ] **Step 6: Prepare but do not launch the destructive hardware experiment**

Append an experiment preregistration with the exact media hashes, pinned EXP241 m1n1/Mu hashes, expected target disk/volume identity, evidence paths, clean-baseline criteria, and recovery path. State that actual formatting requires a new explicit authorization after the WinPE read-only preflight displays the resolved targets.

### Task 7: Authorized reinstall and clean-baseline qualification

**Files:**
- Modify after run: `investigation/EXPERIMENTS.md`
- Modify after proven boundary change: `investigation/GPU_CURRENT_STATE.md`
- Modify after hardware validation: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: separately authorized, hash-pinned USB and pinned EXP241 launch pair.
- Produces: a real Windows install verdict, preserved evidence, clean recovery checkpoint, and an exact GPU-resumption boundary.

- [ ] **Step 1: Boot only to the read-only WinPE preflight**

Record the source/target physical identity, same-disk proof, evidence preservation result, image identity, and proposed operations. Stop if any value differs from preregistration.

- [ ] **Step 2: Request the exact destructive authorization**

Do not submit `ERASE WINDOWS` without explicit authorization naming the displayed `Windows` and `WINESP` targets. This is the only remaining destructive gate.

- [ ] **Step 3: Run the single deployment and capture receipts**

After authorization, submit the exact token once, wait through format/DISM/BCDBoot, capture the final WinPE result, remove the USB, and boot normal Windows once. Do not install or stage AppleAgx.

- [ ] **Step 4: Qualify the clean baseline**

Use SSH key login and require desktop, account/locale, 8 CPUs, healthy NVMe/USBXHCI/AppleInput/APPL0001/VHF, Event129=0, no new bugcheck/reset, loadable hives, one inert APPL0002, and no AppleAgx package/service/module/signer.

- [ ] **Step 5: Create the new recovery checkpoint and resume GPU work**

Save exact SYSTEM/SOFTWARE/DEFAULT/SAM/SECURITY plus transaction logs, BCD, event evidence, driver inventory, hashes, and a System Restore/VSS checkpoint if supported. Update the ledgers with the actual verdict. Resume AppleAgx only from the last hardware-proven GPU boundary and first unknown GPU boundary recorded before recovery.
