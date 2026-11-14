"""Package already-qualified binaries for a Windows UI trial, without compiling."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
def plugin_version():
    cmake = (ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
    parts = []
    for component in ('MAJOR', 'MINOR', 'PATCH'):
        match = re.search(r'set\(VERSION_' + component + r'\s+"(\d+)"\)', cmake)
        if not match:
            raise ValueError('Missing plugin version component: ' + component)
        parts.append(match.group(1))
    return '.'.join(parts)


VERSION = plugin_version()
NAME = 'xWeatherRouting-' + VERSION + '-Windows-UI-Trial'

LAUNCHER = r'''@echo off
setlocal
if not exist "%~dp0OpenCPN\opencpn.exe" (
  echo Please extract the whole ZIP before starting the trial.
  pause
  exit /b 1
)
cd /d "%~dp0OpenCPN" || exit /b 1
for /f "tokens=1 delims==" %%V in ('set WR_HEADLESS_ 2^>nul') do set "%%V="
set "OPENCPN_PLUGIN_DIRS="
set "WR_SHORELINE_DATA_HOME="
set "ENVIRONMENTAL_GRIB_GENERATOR="
set "WR_OPEN_PASSAGE_PLANNER=1"
"%~dp0OpenCPN\opencpn.exe" -p --no_opengl
if errorlevel 1 (
  echo The test copy could not start. Please send Paul OpenCPN\opencpn.log.
  pause
  exit /b 1
)
endlocal
'''

README = '''xWeatherRouting PLUGIN_VERSION - Windows UI trial

Please try the new passage-planning interface and tell Paul what feels clear,
confusing or missing. You do not need to compile or install anything.

1. Extract the whole ZIP to a new folder, for example on your Desktop.
2. Double-click Start-Weather-Routing.cmd. Allow a few seconds for the
   Weather Routing window to open in the included OpenCPN test copy.
3. In Plan passage, click Try example passage (also in Passage actions).
4. Close the planning-help note, then click Calculate route.

The offshore example uses the illustrative 40 ft monohull and bundled
Climatology, so the first calculation needs no GRIB download or extra plugin.
Climatology describes typical conditions, not a current forecast.

Then try Edit route, Boat and polar library, and Passage settings. If you
want to try your own OpenCPN route, add test charts and a copy of that route
to THIS test copy, then try Use OpenCPN route or the route's right-click menu.
Your normal chart collection and routes are not copied into this trial.

Feedback Paul would find useful:
- Was it obvious how to get from Route, Boat and Weather to a calculation?
- Are the readiness messages and the boat/weather choices understandable?
- What took too many clicks, felt awkward, or was hard to read?

Screenshots, your Windows display scaling and the steps that confused you
would help. A build-system review is not needed for this first look.

This is a separate portable test copy. Its settings and test data stay in
this extracted folder. Always use Start-Weather-Routing.cmd. Close the test
windows when finished; delete this extracted folder to remove the trial.
It does not install into or replace your normal OpenCPN or its plugins.

Included: OpenCPN 5.15.0 (e41f0de), wxWidgets 3.3.4, xWeatherRouting PLUGIN_VERSION.
This host is for the UI trial; custom chart-aware hosts are a separate test.
Source and build instructions:
https://github.com/pob220/xweather_routing_pi/blob/SOURCE_COMMIT/docs/v2/WINDOWS.md
Native binary qualification:
https://github.com/pob220/xweather_routing_pi/actions/runs/QUALIFICATION_RUN
OpenCPN and plugin licences are retained with their files. Provenance and
checksums for all distributed files are in trial-manifest.json.
'''


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_amd64(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if (data[:2] != b'MZ' or data[offset:offset + 4] != b'PE\0\0' or
            struct.unpack_from('<H', data, offset + 4)[0] != 0x8664):
        raise ValueError('Expected an AMD64 binary: ' + str(path))


def make(plugin, output, source, run, installer=None):
    if not re.fullmatch(r'[0-9a-f]{40}', source) or run <= 0:
        raise ValueError('Provide the qualified source commit and native CI run')
    sums = plugin.parent / 'SHA256SUMS'
    expected = dict((line.split()[1], line.split()[0]) for line in sums.read_text().splitlines())
    plugin_sha = sha(plugin)
    if expected.get(plugin.name) != plugin_sha or not plugin.name.endswith('-wx334.tar.gz'):
        raise ValueError('Plugin archive does not match the qualified wx334 checksums')
    with tarfile.open(plugin) as archive:
        metadata = ET.fromstring(archive.extractfile('metadata.xml').read())
        if (metadata.findtext('version').strip() != VERSION or
                metadata.findtext('target').strip() != 'msvc-64' or
                metadata.findtext('target-arch').strip() != 'x86_64'):
            raise ValueError('Expected the matching version of the Windows x64 plugin')
    pin = json.loads((ROOT / 'ci/windows64-sdk.json').read_text())
    with tempfile.TemporaryDirectory(prefix='xwr-ui-trial-') as temporary:
        work = Path(temporary)
        if installer is None:
            installer = work / 'opencpn-setup.exe'
            with urllib.request.urlopen(pin['host_url'], timeout=120) as response, installer.open('wb') as stream:
                shutil.copyfileobj(response, stream)
        if sha(installer) != pin['host_sha256']:
            raise ValueError('The OpenCPN host does not match the qualified SDK')
        trial = work / NAME
        host = trial / 'OpenCPN'
        subprocess.run(['7z', 'x', '-y', '-o' + str(host), str(installer)],
                       check=True, stdout=subprocess.DEVNULL)
        # Installer utilities are not part of a portable application. In
        # particular, never ship an uninstaller which might target a normal install.
        for entry in list(host.iterdir()):
            if entry.name.startswith('$') or entry.name.lower().startswith('uninstall'):
                shutil.rmtree(entry) if entry.is_dir() else entry.unlink()
        with tarfile.open(plugin) as archive:
            for entry in archive.getmembers():
                parts = PurePosixPath(entry.name).parts
                if not entry.isfile() or 'plugins' not in parts:
                    continue
                relative = PurePosixPath(*parts[parts.index('plugins'):])
                if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative):
                    raise ValueError('Unsafe package member: ' + entry.name)
                destination = host.joinpath(*relative.parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.extractfile(entry).read())
        # The host installer expects a machine-wide VC runtime. Carry the
        # already-redistributed, qualified helper's CRT next to this private host.
        helper = host / 'plugins/xweather_routing_pi/bin'
        for pattern in ('msvcp140*.dll', 'vcruntime140*.dll', 'concrt140.dll', 'vcomp140.dll'):
            for image in helper.glob(pattern):
                shutil.copy2(image, host / image.name)
        binary = (host / 'opencpn.exe').read_bytes()
        version = re.search(rb'5\.15\.0-0\+[a-f0-9]+\x00', binary)
        dates = re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary)
        if not version or not dates:
            raise ValueError('Missing qualified host version/build date')
        (host / 'opencpn.ini').write_text(
            '[Settings]\nConfigVersionString=Version ' + version[0][:-1].decode() +
            ' Build ' + max(dates)[:-1].decode() + '\nNavMessageShown=1\n'
            'OpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nLocale=en_US\n'
            '[PlugIns/grib_pi.dll]\nbEnabled=0\n'
            '[PlugIns/xweather_routing_pi.dll]\nbEnabled=1\n'
            '[Plugins/xWeatherRouting2]\nConfigVersion=201\n'
            '[PlugIns/xWeatherRouting2]\nConfigVersion=201\n', encoding='ascii')
        (trial / 'Start-Weather-Routing.cmd').write_bytes(LAUNCHER.replace('\n', '\r\n').encode('ascii'))
        (trial / 'READ-ME-FIRST.txt').write_text(README.replace('PLUGIN_VERSION', VERSION).replace('SOURCE_COMMIT', source).replace('QUALIFICATION_RUN', str(run)), encoding='utf-8')
        for image in host.rglob('*'):
            if image.suffix.lower() in ('.exe', '.dll'):
                check_amd64(image)
        files = {p.relative_to(trial).as_posix(): sha(p)
                 for p in sorted(trial.rglob('*')) if p.is_file()}
        (trial / 'trial-manifest.json').write_text(json.dumps({
            'plugin_version': VERSION, 'plugin_source_commit': source,
            'qualification_run': run, 'plugin_archive_sha256': plugin_sha,
            'host': pin, 'portable': True, 'files_sha256': files,
        }, indent=2) + '\n', encoding='utf-8')
        output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(trial.rglob('*')):
                if path.is_file():
                    archive.write(path, path.relative_to(trial).as_posix())
    output.with_name(output.name + '.sha256').write_text(sha(output) + '  ' + output.name + '\n')
    print('Windows UI trial:', output, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host-installer', type=Path)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--qualification-run', type=int, required=True)
    args = parser.parse_args()
    make(args.plugin.resolve(), args.output.resolve(), args.source_commit, args.qualification_run,
         args.host_installer.resolve() if args.host_installer else None)
