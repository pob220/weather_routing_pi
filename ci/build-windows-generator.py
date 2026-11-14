"""Build/test/stage the independent x64 GRIB helper; no OpenCPN installation.

Run from an x64 VS 2022 developer prompt. Dependencies live in .windows-deps,
with an immutable vcpkg checkout and a release-only dynamic triplet.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from windows_build_tools import select_msvc

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.windows-deps'
STAGE = WORK / 'stage'
TRIPLET = 'x64-windows-release'
PIN = 'cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3'  # vcpkg 2026.06.24


def run(*args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def collect_dependency_logs():
    # Do not recursively glob extracted sources: ecbuild contains a Windows
    # directory link which the artifact uploader cannot stat. Port-level logs
    # contain the configure/build diagnostics without following source links.
    buildtrees = WORK / 'vcpkg/buildtrees'
    if not buildtrees.is_dir():
        return
    for port in buildtrees.iterdir():
        if not port.is_dir() or port.is_symlink():
            continue
        for log in port.glob('*.log'):
            destination = WORK / 'logs/vcpkg' / port.name / log.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(log, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependencies-only', action='store_true',
                        help='Prepare/cache dependencies before the helper build')
    args = parser.parse_args()
    visual_studio = select_msvc()
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'binary-cache').mkdir(exist_ok=True)
    logs = WORK / 'logs'
    logs.mkdir(exist_ok=True)
    vcpkg = WORK / 'vcpkg'
    if not (vcpkg / '.git').exists():
        run('git', 'init', vcpkg)
        run('git', '-C', vcpkg, 'remote', 'add', 'origin', 'https://github.com/microsoft/vcpkg.git')
    current = subprocess.check_output(['git', '-C', str(vcpkg), 'rev-parse', 'HEAD'],
                                      text=True, stderr=subprocess.DEVNULL).strip() if (vcpkg / '.git/HEAD').exists() and (vcpkg / 'bootstrap-vcpkg.bat').exists() else ''
    if current != PIN:
        run('git', '-C', vcpkg, 'fetch', '--depth=1', 'origin', PIN)
        run('git', '-C', vcpkg, 'checkout', '--detach', PIN)
    if not (vcpkg / 'vcpkg.exe').is_file() or current != PIN:
        run('cmd', '/c', vcpkg / 'bootstrap-vcpkg.bat', '-disableMetrics')
    manifest = ROOT / 'ci/windows-generator'
    installed = WORK / 'installed'
    run(vcpkg / 'vcpkg.exe', 'install', '--triplet=' + TRIPLET,
        '--x-manifest-root=' + str(manifest), '--x-install-root=' + str(installed),
        '--overlay-triplets=' + str(ROOT / 'ci/vcpkg-triplets'),
        '--overlay-ports=' + str(ROOT / 'ci/vcpkg-overlay-ports'))
    dependencies = subprocess.check_output([str(vcpkg / 'vcpkg.exe'), 'list',
                                           '--x-install-root=' + str(installed)], text=True)
    (logs / 'dependencies.txt').write_text(dependencies, encoding='utf-8')
    if args.dependencies_only:
        print('Windows scientific dependencies ready', flush=True)
        return
    prefix = installed / TRIPLET
    env = dict(os.environ, PATH=str(prefix / 'bin') + os.pathsep + os.environ['PATH'],
               ECCODES_DEFINITION_PATH=str(prefix / 'share/eccodes/definitions'),
               ECCODES_SAMPLES_PATH=str(prefix / 'share/eccodes/samples'),
               PROJ_DATA=str(prefix / 'share/proj'))
    build = WORK / 'build'
    run('cmake', '-S', ROOT / 'third_party/environmental-grib-generator', '-B', build,
        '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_GENERATOR_INSTANCE=' + visual_studio,
        '-DCMAKE_TOOLCHAIN_FILE=' + str(vcpkg / 'scripts/buildsystems/vcpkg.cmake'),
        '-DVCPKG_TARGET_TRIPLET=' + TRIPLET,
        '-DVCPKG_INSTALLED_DIR=' + str(installed),
        '-DVCPKG_MANIFEST_DIR=' + str(manifest), '-DVCPKG_MANIFEST_INSTALL=OFF',
        '-DVCPKG_OVERLAY_TRIPLETS=' + str(ROOT / 'ci/vcpkg-triplets'),
        '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release', env=env)
    run('cmake', '--build', build, '--config', 'Release', '--parallel', '4', env=env)
    run('ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure',
        '--no-tests=error', '--timeout', '180', '--output-junit', logs / 'ctest.xml', env=env)
    # This directory is owned solely by this script, never an installed plugin.
    if STAGE.exists():
        shutil.rmtree(STAGE)
    run('cmake', '--install', build, '--config', 'Release', '--prefix', STAGE, env=env)
    for image in (prefix / 'bin').glob('*.dll'):
        shutil.copy2(image, STAGE / 'bin' / image.name)
    runtime = STAGE / 'runtime'
    eccodes = runtime / 'share/eccodes'
    shutil.copytree(prefix / 'share/eccodes/definitions', eccodes / 'definitions',
                    ignore=shutil.ignore_patterns('bufr', 'metar', 'taf', 'grib3'))
    shutil.copytree(prefix / 'share/eccodes/samples', eccodes / 'samples')
    shutil.copytree(prefix / 'share/proj', runtime / 'share/proj')
    licenses = runtime / 'licenses'
    licenses.mkdir()
    # Include transitive dependency notices as well as the requested ports.
    for copyright in sorted((prefix / 'share').glob('*/copyright')):
        shutil.copy2(copyright, licenses / (copyright.parent.name + '.txt'))
    shutil.copy2(ROOT / 'third_party/environmental-grib-generator/LICENSE', licenses / 'generator.txt')
    shutil.copy2(ROOT / 'ci/windows-generator/vcpkg.json', runtime / 'vcpkg.json')
    source = ROOT / 'third_party/environmental-grib-generator'
    hashes = {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(source.rglob('*')) if p.is_file()}
    (runtime / 'stage.json').write_text(json.dumps({
        'architecture': 'x86_64', 'configuration': 'Release', 'vcpkg_commit': PIN,
        'triplet': TRIPLET, 'source_sha256': hashes,
    }, indent=2) + '\n', encoding='utf-8')
    run(sys.executable, ROOT / 'ci/test-windows-generator.py', STAGE, logs / 'packaged-runtime')
    print('Independent Windows GRIB helper ready:', STAGE, flush=True)


if __name__ == '__main__':
    try:
        main()
    finally:
        collect_dependency_logs()
