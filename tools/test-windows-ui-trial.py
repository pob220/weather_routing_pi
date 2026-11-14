"""Exercise the delivered ZIP/launcher in a disposable native Windows runner."""
import argparse
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def test(archive, plugin, output):
    if sys.platform != 'win32' or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Use a disposable native Windows GitHub Actions runner')
    output.mkdir(parents=True, exist_ok=False)
    destination = output / 'relocated trial with spaces'
    with zipfile.ZipFile(archive) as bundle:
        for name in bundle.namelist():
            parts = PurePosixPath(name)
            if parts.is_absolute() or '..' in parts.parts or ':' in name or '\\' in name:
                raise ValueError('Unsafe trial ZIP entry: ' + name)
        bundle.extractall(destination)
    trial = destination
    host = trial / 'OpenCPN'
    manifest = json.loads((trial / 'trial-manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest['files_sha256'].items():
        if sha(trial / name) != expected:
            raise RuntimeError('Relocated trial checksum mismatch: ' + name)
    if any(p.name.lower().startswith('uninstall') for p in host.iterdir()):
        raise RuntimeError('Portable trial must not contain an uninstaller')

    # Simulate an existing normal profile only in this disposable CI VM.
    # Both launch and route acceptance must leave it byte-for-byte intact.
    sentinels = [Path(os.environ['ProgramData']) / 'opencpn/opencpn.ini',
                 Path(os.environ['LOCALAPPDATA']) / 'opencpn/plugins/trial-keep.txt']
    for path in sentinels:
        if path.exists():
            raise RuntimeError('Refusing to replace an existing profile: ' + str(path))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('[TrialSentinel]\nKeep=existing normal OpenCPN\n', encoding='ascii')
    before = {p: sha(p) for p in sentinels}
    user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
    kernel32.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel32.OpenProcess.restype = w.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
    kernel32.CloseHandle.argtypes = [w.HANDLE]
    user32.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user32.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
    user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    callback_type = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, w.LPARAM]
    user32.EnumChildWindows.argtypes = [w.HWND, callback_type, w.LPARAM]
    user32.IsWindowVisible.argtypes = [w.HWND]
    user32.IsWindowEnabled.argtypes = [w.HWND]
    user32.GetAncestor.argtypes = [w.HWND, w.UINT]
    user32.GetAncestor.restype = w.HWND
    user32.SetForegroundWindow.argtypes = [w.HWND]
    user32.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
    user32.GetWindowLongW.argtypes = [w.HWND, ctypes.c_int]
    user32.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user32.SendMessageW.restype = w.LPARAM
    user32.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
    user32.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_int, w.UINT]

    def text(hwnd):
        buffer = ctypes.create_unicode_buffer(4096)
        user32.GetWindowTextW(hwnd, buffer, len(buffer))
        return buffer.value

    def owned_windows():
        result = []
        @callback_type
        def visit(hwnd, _):
            pid = w.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            handle = kernel32.OpenProcess(0x1000, False, pid.value)
            if handle:
                buffer, length = ctypes.create_unicode_buffer(32768), w.DWORD(32768)
                try:
                    if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(length)):
                        if Path(buffer.value).resolve() == (host / 'opencpn.exe').resolve():
                            result.append((hwnd, text(hwnd)))
                finally:
                    kernel32.CloseHandle(handle)
            return True
        user32.EnumWindows(visit, 0)
        return result

    def children(hwnd):
        result = []
        @callback_type
        def visit(child, _):
            result.append((child, text(child).replace('&', '')))
            return True
        user32.EnumChildWindows(hwnd, visit, 0)
        return result

    def window_state():
        return [{'title': title, 'visible': bool(user32.IsWindowVisible(hwnd)),
                 'enabled': bool(user32.IsWindowEnabled(hwnd)),
                 'children': [{'text': name, 'visible': bool(user32.IsWindowVisible(child)),
                               'enabled': bool(user32.IsWindowEnabled(child))}
                              for child, name in children(hwnd)]}
                for hwnd, title in owned_windows()]

    def class_name(hwnd):
        buffer = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buffer, len(buffer))
        return buffer.value.lower()

    def wait_for(check, seconds=20):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = check()
            if result:
                return result
            time.sleep(0.2)
        (output / 'window-state.json').write_text(json.dumps(window_state(), indent=2), encoding='utf-8')
        raise RuntimeError('Timed out during compact settings acceptance')

    def click_label(label):
        matches = [(child, name) for hwnd, _ in owned_windows()
                   if user32.IsWindowVisible(hwnd)
                   for child, name in children(hwnd)
                   if name == label and user32.IsWindowVisible(child) and user32.IsWindowEnabled(child)]
        if not matches:
            raise RuntimeError('Missing visible control: ' + label)
        button = matches[-1][0]
        # BM_CLICK can fail when its containing dialog is inactive (notably
        # after a child settings window hides). Activate the intended owner
        # before sending the semantic click, just as a pointer click would.
        root_window = user32.GetAncestor(button, 2)  # GA_ROOT
        user32.SetForegroundWindow(root_window)
        time.sleep(0.1)
        print('Click:', label, 'owner:', text(root_window), flush=True)
        user32.PostMessageW(button, 0x00F5, 0, 0)  # BM_CLICK
        time.sleep(0.4)

    def settings_window():
        return next((hwnd for hwnd, title in owned_windows()
                     if title == 'WeatherRouting Configuration'
                     and user32.IsWindowVisible(hwnd)), None)

    def rectangle(hwnd):
        rect = w.RECT()
        if not user32.GetWindowRect(hwnd, ctypes.byref(rect)):
            raise RuntimeError('Cannot read settings window rectangle')
        return (rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top)

    # Only app-local DLLs and the Windows system directory are available.
    environment = dict(os.environ, PATH=str(Path(os.environ['SystemRoot']) / 'System32'))
    # The launcher must discard inherited automation and data-directory overrides.
    environment.update(WR_HEADLESS_ROUTE_TEST='should-be-cleared',
                       WR_HEADLESS_SCENARIO='should-be-cleared',
                       WR_HEADLESS_DATA_DIR=str(output / 'must-not-be-used'))
    with (output / 'launcher.log').open('w') as log:
        process = subprocess.Popen(['cmd', '/d', '/c', str(trial / 'Start-Weather-Routing.cmd')],
                                   cwd=trial, env=environment, stdout=log, stderr=subprocess.STDOUT)
        labels = set()
        try:
            deadline = time.monotonic() + 60
            while time.monotonic() < deadline and process.poll() is None:
                for hwnd, title in owned_windows():
                    labels.add(title)
                    @callback_type
                    def visit(child, _):
                        labels.add(text(child).replace('&', ''))
                        return True
                    user32.EnumChildWindows(hwnd, visit, 0)
                # Native headings exist before the first passage refresh fills in
                # its status labels. Require both, rather than racing that refresh.
                if ({'Passage actions', '1. Route', '2. Boat and polars', '3. Weather', 'Calculate route'} <= labels
                        and any('\u2022' in label for label in labels)):
                    break
                time.sleep(0.25)
            else:
                raise RuntimeError('The delivered launcher did not show the passage planner: ' + repr(sorted(labels)))
            # GetWindowTextW reads the actual labels from the running Windows UI.
            # UTF-8 source text decoded through an ANSI code page used to pass
            # the heading checks while corrupting bullets, arrows and dashes.
            mojibake = ('\u00e2\u20ac', '\u00e2\u2020', '\u00c2\u00b0')
            if any(fragment in label for label in labels for fragment in mojibake):
                raise RuntimeError('Corrupted Unicode in planner labels: ' + repr(sorted(labels)))
            if not any('\u2022' in label for label in labels):
                raise RuntimeError('The planner did not render its Unicode bullet labels')
            click_label('Passage settings')
            settings = wait_for(settings_window)
            user32.SetWindowPos(settings, None, 90, 80, 820, 620, 0x0014)
            time.sleep(0.5)
            saved_rect = rectangle(settings)
            settings_labels = [name for _, name in children(settings)]
            if 'Reset wind/wave limits...' not in settings_labels:
                raise RuntimeError('Missing scoped wind/wave reset')
            draw = [(child, name) for child, name in children(settings)
                    if name.startswith('Use OpenCPN Draw exclusion areas (optional;')]
            if len(draw) != 1 or user32.SendMessageW(draw[0][0], 0x00F0, 0, 0) != 0:
                raise RuntimeError('Fresh passage must leave optional Draw exclusions unchecked')
            if 'Draw unavailable' not in draw[0][1]:
                raise RuntimeError('Draw availability must be explicit')
            click_label('OK')
            wait_for(lambda: not settings_window())
            click_label('Passage settings')
            settings = wait_for(settings_window)
            if rectangle(settings) != saved_rect:
                raise RuntimeError('Settings size/position was not preserved')
            click_label('OK')
            wait_for(lambda: not settings_window())
            click_label('Routing controls')
            settings = wait_for(settings_window)
            # A fresh profile starts Auto, with every membership box checked
            # in both independently saved lists (six existing engines each).
            visible_controls = [(child, name) for child, name in children(settings)
                                if user32.IsWindowVisible(child)]
            # GetWindowTextW cannot reliably read another process's combo
            # selection. Query the eight-item engine selector directly.
            engine_selectors = [child for child, _ in visible_controls
                                if class_name(child) == 'combobox'
                                and user32.SendMessageW(child, 0x0146, 0, 0) == 8]  # CB_GETCOUNT
            if len(engine_selectors) != 1 or user32.SendMessageW(engine_selectors[0], 0x0147, 0, 0) != 0:  # CB_GETCURSEL
                (output / 'window-state.json').write_text(json.dumps(window_state(), indent=2), encoding='utf-8')
                raise RuntimeError('Fresh route did not default to Auto')
            membership = [child for child, name in visible_controls
                          if not name and class_name(child) == 'button'
                          and (user32.GetWindowLongW(child, -16) & 15) in (2, 3, 5, 6)]
            if len(membership) != 12 or any(user32.SendMessageW(child, 0x00F0, 0, 0) != 1
                                           for child in membership):
                (output / 'window-state.json').write_text(json.dumps(window_state(), indent=2), encoding='utf-8')
                raise RuntimeError('Fresh Auto/All membership did not check all six engines')
            # Context help must also work by click/tap without hover or F1.
            click_label('Help')
            help_window = wait_for(lambda: next((hwnd for hwnd, title in owned_windows()
                if title in ('Routing search', 'Land and depth checks', 'Shoreline detail',
                             'Memory and workers') and user32.IsWindowVisible(hwnd)), None))
            click_label('Close')
            wait_for(lambda: not user32.IsWindowVisible(help_window))
            click_label('OK')
            wait_for(lambda: not settings_window())
            # Exercise the actual first-use path, without GRIB/Climatology/Draw
            # plugins. A warning about unknown waves must not disable routing.
            click_label('Try example passage')
            wait_for(lambda: any(title == 'Planning tools' and user32.IsWindowVisible(hwnd)
                                 for hwnd, title in owned_windows()))
            click_label('Close')
            def planner_controls():
                return [(child, name) for hwnd, title in owned_windows()
                        if title.startswith('WeatherRouting -') and user32.IsWindowVisible(hwnd)
                        for child, name in children(hwnd) if user32.IsWindowVisible(child)]
            def ready():
                return any(name == 'Calculate route' and user32.IsWindowEnabled(child)
                           for child, name in planner_controls())
            wait_for(ready)
            click_label('Calculate route')
            wait_for(lambda: any(name.startswith('Complete (') for _, name in planner_controls())
                     and any(name.startswith('Arrival: ') for _, name in planner_controls()), 120)
            click_label('Hide')
            click_label('Passage settings')
            settings = wait_for(settings_window)
            draw = [(child, name) for child, name in children(settings)
                    if name.startswith('Use OpenCPN Draw exclusion areas (optional;')]
            if len(draw) != 1:
                raise RuntimeError('Missing optional Draw checkbox')
            click_label(draw[0][1])
            click_label('OK')
            wait_for(lambda: not settings_window())
            wait_for(lambda: any(name.startswith('Cannot calculate: Draw exclusion checking is enabled')
                                 for _, name in planner_controls()))
            if ready():
                raise RuntimeError('Explicit Draw requirement must block when its provider is missing')
            click_label('Turn off Draw exclusions for this passage')
            wait_for(ready)
            if any('Cannot calculate:' in name for _, name in planner_controls()):
                raise RuntimeError('Direct Draw recovery did not clear the blocker')
            mains = [(hwnd, title) for hwnd, title in owned_windows() if title.startswith('OpenCPN')]
            if not mains:
                raise RuntimeError('Missing owned portable OpenCPN main window')
            user32.PostMessageW(mains[0][0], 0x0010, 0, 0)  # WM_CLOSE, this host only.
            if process.wait(timeout=30) != 0:
                raise RuntimeError('Trial launcher did not exit cleanly')
        finally:
            (output / 'visible-labels.json').write_text(
                json.dumps(sorted(labels), indent=2) + '\n', encoding='utf-8')
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=False,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                process.wait(timeout=10)
    if (output / 'must-not-be-used').exists():
        raise RuntimeError('Launcher inherited the external plugin data override')
    profile = host / 'plugins/xweather_routing_v2'
    if not (profile / 'boats/Example-40ft-monohull.xml').is_file():
        raise RuntimeError('The portable first launch did not populate its own boat library')
    # A copy extracted from the delivered archive also calculates real routes.
    # Exercise the packaged GUI's real adaptive checkbox, mouse handlers,
    # tab refreshes and queued edits on a native Windows host. Product binaries
    # are exactly those from the qualified source; this only adds acceptance.
    adaptive_prefix = (output / 'routes/adaptive-controls').resolve()
    route_environment = dict(os.environ,
        WR_HEADLESS_ADAPTIVE_CONFIGURATION_CONTRACT='1',
        WR_HEADLESS_ADAPTIVE_XML_PREFIX=str(adaptive_prefix))
    subprocess.run([sys.executable, str(ROOT / 'ci/test-windows-host.py'), str(host),
                    str(plugin), str(output / 'routes'), '--host-version', '5.15.0'],
                   env=route_environment, check=True)
    marker = ('WR_ADAPTIVE_CONFIGURATION_CONTRACT passed=1 '
              'modes=Quick,Quick2,Auto,All mouse_policy=1')
    if marker not in (output / 'routes/opencpn.log').read_text(encoding='utf-8', errors='replace'):
        raise RuntimeError('Packaged adaptive GUI control contract did not pass')
    import xml.etree.ElementTree as ET
    contract_scenario = json.loads((output / 'routes/scenario.json').read_text())
    for mode, flag in [('automatic', '1'), ('manual', '0')]:
        snapshot = ET.parse(str(adaptive_prefix) + '-' + mode + '.xml')
        # SaveXML includes the blank new-passage template as well as routes.
        configurations = [c for c in snapshot.findall('Configuration')
                          if c.get('Start') == contract_scenario['start']['name']
                          and c.get('End') == contract_scenario['end']['name']]
        if len(configurations) != 1:
            raise RuntimeError('Expected one matching saved GUI contract route')
        route = configurations[0]
        if not (route.get('AutomaticAdaptiveSettings') == flag and
                float(route.get('dt')) == 5370 and
                float(route.get('OriginalHeadingStepDegrees')) == 7.25):
            raise RuntimeError('Adaptive GUI failed to preserve exact manual values')
    for path, expected in before.items():
        if sha(path) != expected:
            raise RuntimeError('Trial changed the normal profile: ' + str(path))
    (output / 'acceptance.json').write_text(json.dumps({
        'delivered_launcher_tested': True, 'relocated_path_with_spaces': True,
        'normal_profile_preserved': True, 'external_overrides_cleared': True,
        'app_local_crt': True, 'passage_planner_visible': True,
        'unicode_labels_verified': True,
        'compact_settings_geometry_and_click_help': True,
        'fresh_auto_all_engines_checked': True,
        'draw_off_by_default_and_availability_explicit': True,
        'first_use_example_passage_complete_without_other_plugins': True,
        'unavailable_draw_blocks_and_direct_recovery_enables_calculation': True,
        'forecast_climatology_and_fallback_routes': 'complete',
        'adaptive_controls_locked_through_mouse_and_tab_events': True,
        'adaptive_off_restores_exact_manual_values': True,
        'visible_labels': sorted(labels), 'zip_sha256': sha(archive),
    }, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('plugin', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.archive.resolve(), args.plugin.resolve(), args.output.resolve())
