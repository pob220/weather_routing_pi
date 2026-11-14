#!/usr/bin/env python3
"""Launch a private v2 test instance without changing an existing profile."""
import argparse
import os
from pathlib import Path
import shutil
import fcntl

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host", required=True, type=Path)
parser.add_argument("--prefix", required=True, type=Path)
parser.add_argument("--profile", required=True, type=Path)
args = parser.parse_args()
host, prefix, profile = (p.expanduser().resolve() for p in
                         (args.host, args.prefix, args.profile))
library = prefix / "lib/opencpn/libxweather_routing_pi.so"
resources = prefix / "share/opencpn/plugins/xweather_routing_pi"
if not host.is_file() or not library.is_file() or not resources.is_dir():
    parser.error("Select a built OpenCPN test host and an installed v2 prefix.")
marker = profile / ".xweather-v2-test-profile"
if profile.exists() and any(profile.iterdir()) and not marker.exists():
    parser.error("Refusing to reuse a directory which is not a v2 test profile.")
profile.mkdir(parents=True, exist_ok=True)
marker.touch()
instance_lock = (profile / ".xweather-v2-running.lock").open("a")
try:
    fcntl.flock(instance_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    parser.error("This isolated v2 test profile is already running.")
os.set_inheritable(instance_lock.fileno(), True)
plugins = profile / "plugins"
(plugins / "lib").mkdir(parents=True, exist_ok=True)
# Atomic replacement leaves a previous test process's mapped library intact.
temporary = plugins / "lib/.xweather-v2-next.so"
shutil.copy2(library, temporary)
temporary.replace(plugins / "lib/libxweather_routing_pi.so")
data = plugins / "xweather_routing_pi"
if data.is_symlink():
    if data.resolve() != resources:
        data.unlink()
elif data.exists():
    parser.error("Refusing to replace an unmanaged plugin resource directory.")
if not data.exists():
    data.symlink_to(resources, target_is_directory=True)
config = profile / "opencpn.conf"
if not config.exists():
    config.write_text("[Settings]\nNavMessageShown=1\nOpenGL=0\n"
                      "[PlugIns/libxweather_routing_pi.so]\nbEnabled=1\n"
                      "[PlugIns/xWeatherRouting2]\nConfigVersion=200\n")
environment = os.environ.copy()
for key in list(environment):
    if key.startswith("WR_HEADLESS_") or key in (
            "OPENCPN_PLUGIN_DIRS", "XGRIB_PLUGIN_DATA_DIR"):
        environment.pop(key)
environment.update(
    WR_OPEN_PASSAGE_PLANNER="1",
    WR_HEADLESS_DATA_DIR=str(profile / "weather-routing-v2"),
    XDG_CONFIG_HOME=str(profile / "xdg-config"),
    XDG_DATA_HOME=str(profile / "xdg-data"),
    XDG_CACHE_HOME=str(profile / "xdg-cache"))
os.chdir(profile)
os.execve(host, [str(host), "--portable", "--configdir", str(profile),
                 "--no_opengl"], environment)
