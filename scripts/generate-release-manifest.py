#!/usr/bin/env python3
"""Generate the machine-readable Android release manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PROTOCOL_HEADER = ROOT / "include" / "roadcast_protocol.h"
CATALOG_HEADER = ROOT / "include" / "roadcast_catalog.h"
ARTIFACT_NAMES = ("roadcastd", "roadcastctl", "libroadcast_client.so")


def parse_uint_define(path: Path, name: str) -> int:
    pattern = re.compile(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+)u?\s*$",
        re.MULTILINE,
    )
    match = pattern.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"{name} was not found in {path}")
    return int(match.group(1))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as artifact:
        for chunk in iter(lambda: artifact.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def release_metadata(release_tag: str) -> tuple[str, str]:
    if release_tag == "edge":
        return "edge", "edge"
    if release_tag == "ci":
        return "ci", "ci"
    if re.fullmatch(r"v[0-9][0-9A-Za-z.+-]*", release_tag):
        return release_tag.removeprefix("v"), "versioned"
    raise ValueError("release tag must be 'edge', 'ci', or start with 'v'")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--abi", required=True)
    parser.add_argument("--min-android-api", type=int, required=True)
    args = parser.parse_args()

    if not re.fullmatch(r"[0-9a-fA-F]{40}", args.commit):
        raise ValueError("commit must be a full 40-character Git SHA")
    if args.min_android_api < 1:
        raise ValueError("minimum Android API must be positive")

    version, channel = release_metadata(args.release_tag)
    protocol_version = parse_uint_define(
        PROTOCOL_HEADER,
        "ROADCAST_PROTOCOL_VERSION",
    )
    schema_version = parse_uint_define(
        CATALOG_HEADER,
        "ROADCAST_SCHEMA_VERSION",
    )

    artifacts: dict[str, dict[str, object]] = {}
    for name in ARTIFACT_NAMES:
        path = args.dist / name
        if not path.is_file():
            raise FileNotFoundError(f"release artifact not found: {path}")
        artifacts[name] = {
            "file": name,
            "sha256": sha256(path),
            "sizeBytes": path.stat().st_size,
        }

    manifest = {
        "manifestVersion": 1,
        "version": version,
        "releaseTag": args.release_tag,
        "channel": channel,
        "commit": args.commit.lower(),
        "abi": args.abi,
        "minAndroidApi": args.min_android_api,
        "protocol": protocol_version,
        "minClientProtocol": protocol_version,
        "maxClientProtocol": protocol_version,
        "schemaVersion": schema_version,
        "artifacts": artifacts,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
