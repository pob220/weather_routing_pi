"""Check the app-local packaged helper without vcpkg/build-tool DLLs on PATH."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]


def test(stage, output):
    stage, output = stage.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary = stage / 'bin'
    runtime = stage / 'runtime/share'
    system = Path(os.environ['SystemRoot']) / 'System32'
    env = dict(os.environ, PATH=str(system),
               ECCODES_DEFINITION_PATH=str(runtime / 'eccodes/definitions'),
               ECCODES_SAMPLES_PATH=str(runtime / 'eccodes/samples'),
               PROJ_DATA=str(runtime / 'proj'))
    for key in ('ECCODES_EXTRA_DEFINITION_PATH', 'PROJ_LIB'):
        env.pop(key, None)
    # Inspect the complete dependency closure, including delay-load imports.
    # DLLs must be beside the helper or in the Windows system directory.
    imports = {}
    for image in sorted(binary.iterdir()):
        if image.suffix.lower() not in ('.exe', '.dll'):
            continue
        data = image.read_bytes()
        if data[:2] != b'MZ':
            raise RuntimeError('Invalid PE image: ' + image.name)
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset + 4] != b'PE\0\0' or struct.unpack_from('<H', data, offset + 4)[0] != 0x8664:
            raise RuntimeError('Helper runtime must be entirely AMD64: ' + image.name)
        text = subprocess.check_output(['dumpbin', '/dependents', str(image)], text=True)
        names = re.findall(r'^\s+([\w.+-]+\.dll)\s*$', text, re.M | re.I)
        for name in names:
            if name.lower().startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            if not (binary / name).is_file() and not (system / name).is_file():
                raise RuntimeError(f'{image.name} has an unbundled dependency: {name}')
        imports[image.name] = names
    if not (binary / 'vcomp140.dll').is_file():
        raise RuntimeError('The packaged OpenMP runtime is missing')
    helper = binary / 'environmental-grib.exe'

    def command(name, *args):
        completed = subprocess.run([str(helper), *map(str, args)], env=env, cwd=output,
                                   text=True, capture_output=True, timeout=60)
        (output / (name + '.log')).write_text(completed.stdout + completed.stderr, encoding='utf-8')
        if completed.returncode:
            raise RuntimeError(f'Packaged helper {name} failed: {completed.returncode}; {completed.stderr}')
        if args[0] == 'run-job':
            return None  # Progress is newline-delimited JSON; result is atomic.
        return json.loads(completed.stdout)

    command('capabilities', 'capabilities')
    command('inspect-wind', 'inspect-grib', ROOT / 'ci/testdata/irish-sea-uniform-wind.grb')
    # Writes a new GRIB using bundled ecCodes definitions/samples, no network.
    subprocess.run([sys.executable, str(ROOT / 'third_party/environmental-grib-generator/tests/preflight_cli_tests.py'),
                    str(helper)], env=env, cwd=output, check=True, timeout=90)
    job = output / 'job.json'
    result = output / 'result.json'
    grib = output / 'synthetic-current.grb'
    job.write_text(json.dumps({'schemaVersion': 1, 'operation': 'generateEnvironment', 'request': {
        'bbox': {'west': -6, 'south': 53, 'east': -5, 'north': 54},
        'start': '2026-09-28T00:00:00Z', 'hours': 6, 'stepHours': 3,
        'weatherProvider': 'none', 'currentSource': 'synthetic',
        'currentGridSpacingDeg': 0.5, 'timePolicy': 'review', 'output': str(grib),
        'overwrite': True}}), encoding='utf-8')
    command('generate', 'run-job', '--job', job, '--result', result)
    if json.loads(result.read_text())['status'] != 'complete' or not grib.is_file():
        raise RuntimeError('Packaged helper did not generate a complete GRIB')
    command('inspect-generated', 'inspect-grib', grib)
    (output / 'acceptance.json').write_text(json.dumps({
        'app_local_runtime': True, 'build_dependencies_on_path': False,
        'capabilities': True, 'read_grib': True, 'generate_grib_offline': True,
        'dll_imports': imports,
    }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.stage, args.output)
