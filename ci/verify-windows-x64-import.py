"""Import an exact candidate tarball using genuine official Windows hosts.

Run on a disposable GitHub Windows runner. LOCALAPPDATA redirects installed
plugin DLLs/data to this job's temporary directory. No DLLs are copied into
the host by this test: successful installation must come from import-plugin.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET


def download(url, path, expected):
    if not url.startswith('https://'):
        raise ValueError('HTTPS required')
    with urllib.request.urlopen(url, timeout=120) as response, path.open('wb') as stream:
        shutil.copyfileobj(response, stream)
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise ValueError(f'Checksum mismatch: {path.name}')


def run(args, cwd, environment):
    result = subprocess.run(list(map(str, args)), cwd=cwd, env=environment,
                            text=True, capture_output=True, timeout=90)
    print(' '.join(map(str, args)), result.returncode,
          result.stdout, result.stderr, flush=True)
    return result


def main():
    if sys.platform != 'win32':
        raise SystemExit('Native Windows required')
    parser = argparse.ArgumentParser()
    parser.add_argument('--url', required=True)
    parser.add_argument('--sha256', required=True)
    args = parser.parse_args()
    output = Path('windows-import-evidence').resolve()
    output.mkdir(exist_ok=True)
    archive = output / 'candidate.tar.gz'
    download(args.url, archive, args.sha256)
    with tarfile.open(archive) as tar:
        metadata = ET.fromstring(tar.extractfile('metadata.xml').read())
        fields = {key: metadata.findtext(key, '').strip() for key in
                  ['name', 'version', 'api-version', 'target', 'target-version', 'target-arch']}
        assert fields['version'] == '1.34.0'
        assert fields['target'] == 'msvc-64' and fields['target-arch'] == 'x86_64'
        dlls = [member for member in tar if member.name.endswith('_pi.dll')]
        assert len(dlls) == 1
        dll_name = Path(dlls[0].name).name
        dll = tar.extractfile(dlls[0]).read()
        pe = struct.unpack_from('<I', dll, 0x3c)[0]
        assert dll[pe:pe + 4] == b'PE\0\0'
        assert struct.unpack_from('<H', dll, pe + 4)[0] == 0x8664
        dll_hash = hashlib.sha256(dll).hexdigest()
    hosts = [
        ('official-5.14.2-x64',
         'https://dl.cloudsmith.io/public/david-register/opencpn-unstable/raw/files/'
         'opencpn_5.14.2-0+16.bd6986a_setup_x64.exe',
         '8913ab09d1cceac55ac9e402887e9e79650164b9db57a14d0709aac1fb891650'),
        ('official-5.15.0-x64-02cf533',
         'https://dl.cloudsmith.io/public/david-register/opencpn-unstable/raw/files/'
         'opencpn_5.15.0-0+79.02cf533_setup_x64.exe',
         '5f8ae29f7f433e0f6482923db723931c2d63ce9ae44406efe121a68c88d2f62e'),
    ]
    results = []
    for label, url, digest in hosts:
        installer = output / (label + '.exe')
        download(url, installer, digest)
        host = output / label
        subprocess.run(['7z', 'x', '-y', '-o' + str(host), str(installer)],
                       check=True, stdout=subprocess.DEVNULL)
        cli = host / 'opencpn-cmd.exe'
        assert cli.is_file()
        profile = output / (label + '-profile')
        environment = dict(os.environ)
        environment.pop('OPENCPN_COMPAT_TARGET', None)
        for variable, suffix in [('LOCALAPPDATA', 'local'), ('APPDATA', 'roaming')]:
            directory = profile / suffix
            directory.mkdir(parents=True)
            environment[variable] = str(directory)
        abi = run([cli, 'print-abi'], host, environment)
        assert abi.returncode == 0 and abi.stdout.strip() == 'msvc-64:10', abi
        rejected = run([cli, '-a', 'msvc-wx32:10', 'import-plugin', archive], host, environment)
        assert rejected.returncode != 0
        assert 'Incompatible plugin detected' in rejected.stderr
        assert not list(profile.rglob(dll_name))
        imported = run([cli, 'import-plugin', archive], host, environment)
        assert imported.returncode == 0, imported
        installed = list(profile.rglob(dll_name))
        assert len(installed) == 1, installed
        assert hashlib.sha256(installed[0].read_bytes()).hexdigest() == dll_hash
        results.append(dict(host=label, installer_url=url, installer_sha256=digest,
                            compiled_abi=abi.stdout.strip(), default_import_passed=True,
                            obsolete_override_rejected=True, installed_dll=str(installed[0]),
                            installed_dll_sha256=dll_hash))
    receipt = dict(package_url=args.url, package_sha256=args.sha256, metadata=fields,
                   actual_windows_import=True, results=results)
    (output / 'import-acceptance.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == '__main__':
    main()
