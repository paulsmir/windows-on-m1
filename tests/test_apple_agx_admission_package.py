from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "drivers" / "apple-agx" / "admission"
WORKFLOW = ROOT / ".github" / "workflows" / "apple-agx-wdk.yml"


class AppleAgxAdmissionPackageTests(unittest.TestCase):
    def read(self, relative):
        return (PACKAGE / relative).read_text()

    def test_separate_minimal_package_exists(self):
        for relative in (
            "AppleAgxAdmission.vcxproj",
            "AppleAgxAdmission.inf",
            "include/apple_agx_admission.h",
            "src/driver.c",
            "src/lifecycle.c",
        ):
            self.assertTrue((PACKAGE / relative).is_file(), relative)

    def test_project_contains_only_the_admission_layer(self):
        project = self.read("AppleAgxAdmission.vcxproj")
        sources = re.findall(r'<ClCompile Include="([^"]+)"', project)
        self.assertEqual(sources, [r"src\driver.c", r"src\lifecycle.c"])
        self.assertNotIn(r"..\shared", project)
        self.assertNotIn("AppleAgx.vcxproj", project)
        self.assertIn(
            "DXGKDDI_INTERFACE_VERSION=DXGKDDI_INTERFACE_VERSION_WDDM3_0",
            project,
        )
        self.assertIn("displib.lib", project)

    def test_inf_binds_only_appl0002_with_a_distinct_service(self):
        inf = self.read("AppleAgxAdmission.inf")
        self.assertIn("Class=Display", inf)
        self.assertIn("ACPI\\APPL0002", inf)
        self.assertIn("CatalogFile=AppleAgxAdmission.cat", inf)
        self.assertIn("AddService=AppleAgxAdmission", inf)
        self.assertIn("ServiceBinary=%12%\\AppleAgxAdmission.sys", inf)
        self.assertNotIn("AddService=AppleAgx,", inf)
        self.assertNotIn("PCI\\", inf)

    def test_driver_entry_is_wddm3_and_registers_required_admission_callbacks(self):
        driver = self.read("src/driver.c")
        self.assertIn(
            "C_ASSERT(sizeof(DRIVER_INITIALIZATION_DATA) == 1296)", driver
        )
        self.assertIn(
            "initialization.Version = DXGKDDI_INTERFACE_VERSION_WDDM3_0",
            driver,
        )
        expected = {
            "DxgkDdiAddDevice",
            "DxgkDdiStartDevice",
            "DxgkDdiStopDevice",
            "DxgkDdiRemoveDevice",
            "DxgkDdiDispatchIoRequest",
            "DxgkDdiInterruptRoutine",
            "DxgkDdiDpcRoutine",
            "DxgkDdiQueryChildRelations",
            "DxgkDdiQueryChildStatus",
            "DxgkDdiQueryDeviceDescriptor",
            "DxgkDdiSetPowerState",
            "DxgkDdiResetDevice",
            "DxgkDdiUnload",
        }
        assigned = set(re.findall(r"initialization\.(DxgkDdi\w+)\s*=", driver))
        self.assertEqual(assigned, expected)
        self.assertRegex(driver, r"status\s*=\s*DxgkInitialize\(")
        self.assertRegex(driver, r"return\s+status\s*;")

    def test_required_admission_callbacks_are_inert_and_fail_closed(self):
        lifecycle = self.read("src/lifecycle.c")
        header = self.read("include/apple_agx_admission.h")
        for callback in (
            "AppleAgxAdmissionInterruptRoutine",
            "AppleAgxAdmissionDpcRoutine",
            "AppleAgxAdmissionQueryChildRelations",
            "AppleAgxAdmissionQueryChildStatus",
            "AppleAgxAdmissionQueryDeviceDescriptor",
            "AppleAgxAdmissionSetPowerState",
            "AppleAgxAdmissionResetDevice",
        ):
            self.assertIn(callback, header)
            self.assertIn(callback, lifecycle)
        self.assertRegex(
            lifecycle,
            r"AppleAgxAdmissionInterruptRoutine[\s\S]*?return FALSE;",
        )
        self.assertRegex(
            lifecycle,
            r"AppleAgxAdmissionQueryChildRelations[\s\S]*?return STATUS_SUCCESS;",
        )
        self.assertRegex(
            lifecycle,
            r"AppleAgxAdmissionQueryChildStatus[\s\S]*?return STATUS_NOT_SUPPORTED;",
        )
        self.assertRegex(
            lifecycle,
            r"AppleAgxAdmissionQueryDeviceDescriptor[\s\S]*?return STATUS_NOT_SUPPORTED;",
        )

    def test_lifecycle_is_persistent_and_start_fails_closed(self):
        lifecycle = self.read("src/lifecycle.c")
        self.assertIn("IoOpenDeviceRegistryKey", lifecycle)
        self.assertIn("ZwSetValueKey", lifecycle)
        self.assertIn("Wom1AdmissionAddDeviceStage", lifecycle)
        self.assertIn("Wom1AdmissionStartDeviceStage", lifecycle)
        self.assertIn("Wom1AdmissionStartDeviceStatus", lifecycle)
        self.assertIn("*NumberOfVideoPresentSources = 0", lifecycle)
        self.assertIn("*NumberOfChildren = 0", lifecycle)
        self.assertIn("return STATUS_NOT_SUPPORTED", lifecycle)

    def test_admission_layer_cannot_touch_gpu_hardware(self):
        sources = self.read("src/driver.c") + self.read("src/lifecycle.c")
        forbidden = (
            "DxgkCbGetDeviceInformation",
            "DxgkCbMapMemory",
            "MmMapIoSpace",
            "READ_REGISTER",
            "WRITE_REGISTER",
            "RTKit",
            "UAT",
            "mailbox",
            "firmware",
            "DxgkDdiPresent",
            "DxgkDdiRender",
        )
        lowered = sources.lower()
        for token in forbidden:
            self.assertNotIn(token.lower(), lowered, token)

    def test_pinned_wdk_ci_builds_and_publishes_the_admission_package(self):
        workflow = WORKFLOW.read_text()
        self.assertIn("build-admission-arm64:", workflow)
        self.assertIn(
            r"msbuild drivers\apple-agx\admission\AppleAgxAdmission.vcxproj",
            workflow,
        )
        self.assertIn("AppleAgxAdmission-ARM64-Debug", workflow)
        self.assertIn(
            "drivers/apple-agx/admission/ARM64/Debug/**/*.sys", workflow
        )
        self.assertIn("RunCodeAnalysis=true", workflow)

    def test_admission_ci_verifies_and_publishes_signature_provenance(self):
        workflow = WORKFLOW.read_text()
        admission = workflow[workflow.index("  build-admission-arm64:") :]
        self.assertIn(
            "Verify WDK admission signature provenance",
            admission,
        )
        self.assertIn("AppleAgxAdmission-signature.json", admission)
        self.assertIn("Get-AuthenticodeSignature", admission)
        self.assertIn("Get-PfxCertificate", admission)
        self.assertIn("SignerCertificate.Thumbprint", admission)
        self.assertIn("certificate.Thumbprint", admission)
        self.assertIn("catalog_sha256", admission)
        self.assertIn("certificate_sha256", admission)
        self.assertIn("source_commit", admission)
        self.assertIn("github_run_id", admission)
        self.assertIn(
            "drivers/apple-agx/admission/ARM64/Debug/**/*-signature.json",
            admission,
        )


if __name__ == "__main__":
    unittest.main()
