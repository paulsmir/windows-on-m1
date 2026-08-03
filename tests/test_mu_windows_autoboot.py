from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "mu/Silicon/Apple/AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootDxe.c"
INF = ROOT / "mu/Silicon/Apple/AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootDxe.inf"
DSC = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc"
FDF = ROOT / "mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf"


def test_windows_autoboot_driver_is_packaged_and_mapping_independent():
    source = DRIVER.read_text()
    inf = INF.read_text()
    dsc = DSC.read_text()
    fdf = FDF.read_text()

    module = "AppleSiliconPkg/Drivers/WindowsAutoBootDxe/WindowsAutoBootDxe.inf"
    assert module in dsc
    assert f"INF {module}" in fdf
    assert "MODULE_TYPE                    = DXE_DRIVER" in inf
    assert "gEfiEventReadyToBootGuid" in source
    assert "gEfiSimpleFileSystemProtocolGuid" in source
    assert "RegisterProtocolNotify" in source
    assert "mSimpleFileSystemRegistration" in source
    assert "mWindowsStarted" in source
    assert "gEfiBlockIoProtocolGuid" in source
    assert "ConnectController" in source
    assert "RemovableMedia" in source
    assert "WINDOWS_FALLBACK_LOADER" in source
    assert "BOOTAA64.EFI" in source
    assert "LoadOptions = NULL" in source
    assert "LoadOptionsSize = 0" in source
    assert "FS3:" not in source


if __name__ == "__main__":
    test_windows_autoboot_driver_is_packaged_and_mapping_independent()
    print("test_mu_windows_autoboot: ok")
