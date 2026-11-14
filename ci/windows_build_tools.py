"""Keep the plugin, helper and vcpkg on the active VS 2022 x64 toolchain."""
import os
from pathlib import Path
import shutil
import sys


def select_msvc():
    version = os.environ.get('VSCMD_VER', '')
    architecture = os.environ.get('VSCMD_ARG_TGT_ARCH', '').lower()
    installation = os.environ.get('VSINSTALLDIR', '')
    if (sys.platform != 'win32' or not version.startswith('17.') or
            architecture not in ('x64', 'amd64') or not installation or
            not shutil.which('cl')):
        raise SystemExit('Use an x64 Native Tools Command Prompt for VS 2022')
    instance = str(Path(installation).resolve())
    # Without this, vcpkg may choose a side-by-side VS 2026 installation.
    os.environ['VCPKG_VISUAL_STUDIO_PATH'] = instance
    print('Using VS 2022 x64 for all builds:', instance, flush=True)
    return instance
