"""
Génère le fichier .ota, crée une GitHub Release et met à jour ota_index.json.

Usage :
  python generate_ota.py <version> <github_token>

  version      : entier (ex: 2)
  github_token : Personal Access Token GitHub (scope: repo)

Exemple :
  python generate_ota.py 2 ghp_xxxxxxxxxxxxxxxxxxxx
"""

import struct, hashlib, json, os, sys, subprocess, urllib.request, urllib.error

# ── Config ────────────────────────────────────────────────
MANUFACTURER_CODE = 0x131B
IMAGE_TYPE        = 0x0000
HEADER_STRING     = "Levoit Classic 300S Zigbee"
INPUT_BIN         = "build/levoit_zigbee.bin"

GITHUB_REPO       = "Flo69au/levoit-classic-300s-zigbee"
GITHUB_BRANCH     = "main"
OTA_INDEX_FILE    = "ota_index.json"   # dans le repo
# ─────────────────────────────────────────────────────────


def make_ota_bytes(fw_data, version):
    header_str = HEADER_STRING.encode("ascii").ljust(32, b"\x00")[:32]
    subelement = struct.pack("<HI", 0x0000, len(fw_data)) + fw_data
    header_len = 56
    total_size = header_len + len(subelement)
    header = struct.pack(
        "<IHHHHHhI32sI",
        0x0BEEF11E, 0x0100, header_len, 0x0000,
        MANUFACTURER_CODE, IMAGE_TYPE, 0x0002,
        version, header_str, total_size,
    )
    return header + subelement


def github_api(token, method, path, data=None, raw=False):
    url = f"https://api.github.com{path}"
    req = urllib.request.Request(url)
    req.method = method
    req.add_header("Authorization", f"token {token}")
    req.add_header("Accept", "application/vnd.github+json")
    if data and not raw:
        body = json.dumps(data).encode()
        req.add_header("Content-Type", "application/json")
        req.data = body
    elif raw:
        req.data = data
        req.add_header("Content-Type", "application/octet-stream")
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        print(f"GitHub API error {e.code}: {e.read().decode()}")
        sys.exit(1)


def upload_asset(token, upload_url, filename, data):
    # upload_url contient {?name,label} — on le nettoie
    base_url = upload_url.split("{")[0]
    url = f"{base_url}?name={filename}"
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Authorization", f"token {token}")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Accept", "application/vnd.github+json")
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    version = int(sys.argv[1])
    token   = sys.argv[2]
    tag     = f"v1.0.0.{version}"
    ota_name = f"levoit_300s_{tag}.ota"

    # ── 1. Lire le binaire ────────────────────────────────
    if not os.path.exists(INPUT_BIN):
        print(f"Erreur : {INPUT_BIN} introuvable. Lancez 'idf.py build' d'abord.")
        sys.exit(1)

    with open(INPUT_BIN, "rb") as f:
        fw_data = f.read()

    print(f"Firmware   : {len(fw_data):,} bytes")

    # ── 2. Générer le fichier .ota ────────────────────────
    ota_data = make_ota_bytes(fw_data, version)
    sha512   = hashlib.sha512(ota_data).hexdigest()
    print(f"OTA généré : {len(ota_data):,} bytes  version={version} (0x{version:08X})")

    # ── 3. Créer la GitHub Release ────────────────────────
    print(f"\nCréation de la release {tag} sur GitHub...")
    release = github_api(token, "POST", f"/repos/{GITHUB_REPO}/releases", {
        "tag_name":   tag,
        "name":       f"Firmware {tag}",
        "body":       f"Levoit Classic 300S Zigbee — OTA {tag}\n\nVersion Zigbee : {version} (0x{version:08X})",
        "draft":      False,
        "prerelease": False,
    })
    print(f"Release créée : {release['html_url']}")

    # ── 4. Uploader le .ota ───────────────────────────────
    print(f"Upload de {ota_name}...")
    asset = upload_asset(token, release["upload_url"], ota_name, ota_data)
    download_url = asset["browser_download_url"]
    print(f"URL : {download_url}")

    # ── 5. Mettre à jour ota_index.json ───────────────────
    index_url = (
        f"https://raw.githubusercontent.com/{GITHUB_REPO}"
        f"/{GITHUB_BRANCH}/{OTA_INDEX_FILE}"
    )
    index = [{
        "fileName":         ota_name,
        "fileVersion":      version,
        "fileSize":         len(ota_data),
        "manufacturerCode": MANUFACTURER_CODE,
        "imageType":        IMAGE_TYPE,
        "sha512":           sha512,
        "url":              download_url,
    }]

    with open(OTA_INDEX_FILE, "w") as f:
        json.dump(index, f, indent=2)
    print(f"\nota_index.json mis à jour")

    # ── 6. Commit + push ota_index.json ───────────────────
    print("Push de ota_index.json vers GitHub...")
    subprocess.run(["git", "add", OTA_INDEX_FILE], check=True)
    subprocess.run(["git", "commit", "-m", f"OTA index — firmware {tag}"], check=True)
    subprocess.run(["git", "push"], check=True)
    print("Done ✓")

    # ── 7. Résumé ─────────────────────────────────────────
    print(f"""
═══════════════════════════════════════════════════
 OTA prêt !
 Index Z2M : {index_url}
═══════════════════════════════════════════════════
Dans configuration.yaml de Zigbee2MQTT :

  ota:
    zigbee_ota_override_index_location: {index_url}

Redémarrez Z2M puis : device levoit → OTA → Check
""")


if __name__ == "__main__":
    main()
