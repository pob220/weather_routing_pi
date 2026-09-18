#!/usr/bin/env python3
"""Add OpenCPN's required metadata.xml to a CPack Android plugin archive."""

import io
import sys
import tarfile
import xml.etree.ElementTree as ET
from pathlib import Path


def main(package_path: Path, xml_path: Path, output_dir: Path) -> None:
    metadata = xml_path.read_bytes()
    root = ET.fromstring(metadata)
    assert root.findtext("target", "").strip() == "android-arm64"
    assert root.findtext("api-version", "").strip() == "1.21"
    assert root.findtext("version", "").strip() == "1.17.12.0"
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / package_path.name.replace(".tar.gz", "-import.tar.gz")
    with tarfile.open(package_path, "r:gz") as source, tarfile.open(
        output, "w:gz"
    ) as destination:
        info = tarfile.TarInfo("metadata.xml")
        info.size = len(metadata)
        info.mode = 0o644
        destination.addfile(info, io.BytesIO(metadata))
        names = set()
        for member in source:
            names.add(member.name)
            destination.addfile(member, source.extractfile(member) if member.isfile() else None)
    assert any(name.endswith("/lib/opencpn/libweather_routing_pi.so") for name in names)
    assert any("/data/shoreline/manifest.json" in name for name in names)
    print(output)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("usage: package-android-import.py PACKAGE XML OUTPUT_DIR")
    main(*(Path(argument) for argument in sys.argv[1:]))
