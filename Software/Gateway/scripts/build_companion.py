"""Generate the compiled Gateway Companion bundle before every firmware build."""

from pathlib import Path
import shutil
import subprocess


Import("env")


def build_companion():
    gateway_dir = Path(env["PROJECT_DIR"])
    companion_dir = gateway_dir.parent / "Companion"
    npm = shutil.which("npm.cmd") or shutil.which("npm")
    if npm is None:
        raise RuntimeError(
            "npm is required to build the Gateway Companion. Install Node.js, then run npm ci in Software/Companion."
        )

    print("Generating compiled FlexBMS Companion Gateway bundle")
    subprocess.run([npm, "run", "build:gateway"], cwd=companion_dir, check=True)


# Extra scripts are loaded before PlatformIO evaluates C++ source dependencies.
# Running here means a changed GatewayAssets.cpp is compiled into this exact
# firmware image, including when the previous firmware build was up-to-date.
# This also applies to `pio run -t upload`, which first performs the normal
# build phase.
build_companion()
