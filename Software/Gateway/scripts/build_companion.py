"""Generate the compiled Gateway Companion bundle before every firmware build."""

from pathlib import Path
import shutil
import subprocess


Import("env")


def ensure_time_sync_config():
    """Keep persisted PlatformIO sdkconfig aligned with the NTP requirements."""
    gateway_dir = Path(env["PROJECT_DIR"])
    sdkconfig = gateway_dir / f"sdkconfig.{env['PIOENV']}"
    if not sdkconfig.exists():
        return  # sdkconfig.defaults supplies these values during first configuration.

    required = {
        "CONFIG_LWIP_SNTP_MAX_SERVERS": "4",
        "CONFIG_LWIP_SNTP_UPDATE_DELAY": "3600000",
    }
    lines = sdkconfig.read_text(encoding="utf-8").splitlines()
    seen = set()
    updated = []
    for line in lines:
        key = line.split("=", 1)[0]
        if key in required:
            updated.append(f"{key}={required[key]}")
            seen.add(key)
        else:
            updated.append(line)
    updated.extend(f"{key}={value}" for key, value in required.items() if key not in seen)
    if updated != lines:
        sdkconfig.write_text("\n".join(updated) + "\n", encoding="utf-8")


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
ensure_time_sync_config()
build_companion()
