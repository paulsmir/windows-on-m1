"""Read AGX lab configuration values for the Python helpers.

The environment wins; otherwise the value comes from lib.sh, which sources the
private lab configuration (see lab.env.example).
"""
import os
import subprocess
from pathlib import Path

LAB_DIR = Path(__file__).resolve().parent
PACKAGE = ["AppleAgxRenderAdmission.inf", "AppleAgxRenderAdmission.sys",
           "AppleAgxRenderAdmissionUmd.dll", "appleagxrenderadmission.cat"]


def lab_env(name):
    if os.environ.get(name):
        return os.environ[name]
    value = subprocess.run(
        ["bash", "-c", '. "$0" >/dev/null && printf %s "${!1}"', str(LAB_DIR / "lib.sh"), name],
        check=True, capture_output=True, text=True).stdout
    if not value:
        raise SystemExit(f"agx-lab: {name} is not configured")
    return value


def experiments():
    return Path(lab_env("AGX_EXPERIMENTS"))
