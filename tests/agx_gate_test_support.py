from pathlib import Path
import shlex
import shutil


def write_artifact_fixture(path: Path, name: str, index: int, contract) -> None:
    """Write a minimal artifact that still satisfies production identity gates."""
    payload = f"fixture-{index}-{name}\n".encode()
    if name == "m1n1.macho":
        payload += (
            b"##m1n1_ver##" + contract.source.m1n1_commit[:7].encode() + b"\0"
        )
    path.write_bytes(payload)


def install_contract_git(test_bin: Path, contract) -> None:
    """Make operator fixtures independent of the development checkout pins."""
    real_git = shutil.which("git")
    if real_git is None:
        raise RuntimeError("git is required by the AGX operator fixtures")
    script = test_bin / "git"
    script.write_text(
        "#!/bin/sh\n"
        "if [ \"$1\" = -C ] && [ \"$3\" = rev-parse ] && "
        "[ \"$4\" = HEAD ]; then\n"
        "  case \"$2\" in\n"
        f"    */m1n1_windows) echo {contract.source.m1n1_commit}; exit 0 ;;\n"
        f"    */mu) echo {contract.source.mu_commit}; exit 0 ;;\n"
        "  esac\n"
        "fi\n"
        "if [ \"$1\" = -C ] && [ \"$3\" = cat-file ] && "
        "[ \"$4\" = -e ]; then\n"
        "  exit 0\n"
        "fi\n"
        f"exec {shlex.quote(real_git)} \"$@\"\n"
    )
    script.chmod(0o755)
