#!/usr/bin/env python3
import json
import pathlib
import urllib.request

MANIFEST_URL = "https://launchermeta.mojang.com/mc/game/version_manifest_v2.json"
TARGETS = {"1.7.10", "1.7.4", "b1.7.3", "a1.2.6"}

with urllib.request.urlopen(MANIFEST_URL, timeout=30) as response:
    manifest = json.load(response)

out_dir = pathlib.Path(__file__).resolve().parents[1] / "docs" / "legacy-metadata"
out_dir.mkdir(parents=True, exist_ok=True)
summary = []
for version in manifest["versions"]:
    if version["id"] not in TARGETS:
        continue
    with urllib.request.urlopen(version["url"], timeout=30) as response:
        metadata = json.load(response)
    (out_dir / f"{version['id']}.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    downloads = metadata.get("downloads", {})
    asset_index = metadata.get("assetIndex", {})
    summary.append({
        "id": version["id"],
        "manifest_type": version["type"],
        "metadata_type": metadata.get("type"),
        "inheritsFrom": metadata.get("inheritsFrom"),
        "has_client_download": "client" in downloads,
        "has_asset_index": bool(asset_index),
        "asset_index_id": asset_index.get("id"),
        "libraries": len(metadata.get("libraries", [])),
        "mainClass": metadata.get("mainClass"),
        "minimumLauncherVersion": metadata.get("minimumLauncherVersion"),
        "javaVersion": metadata.get("javaVersion"),
    })

(out_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
missing = TARGETS - {entry["id"] for entry in summary}
if missing:
    print("Missing from manifest:", ", ".join(sorted(missing)))
