#!/bin/bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
node_bin="$(command -v node || true)"
if [[ -z "$node_bin" ]]; then
  echo "node is not on PATH. Install Node, then run this again." >&2
  exit 1
fi

dest="${HOME}/Library/Application Support/Google/Chrome/NativeMessagingHosts"
mkdir -p "$dest"
launcher="${dest}/kchain-native-host.sh"
node_q="$(printf '%q' "$node_bin")"
script_q="$(printf '%q' "$root/extension/native/kchain-native-host.mjs")"
cat > "$launcher" <<EOF
#!/bin/bash
exec ${node_q} ${script_q}
EOF
chmod +x "$launcher"

cat > "${dest}/com.kchain.usb.json" <<EOF
{
  "name": "com.kchain.usb",
  "description": "kChain USB typer",
  "path": "${launcher}",
  "type": "stdio",
  "allowed_origins": [
    "chrome-extension://akhgabpnnjnkhgpckaokepjcgdhiablm/"
  ]
}
EOF

echo "Installed the kChain native host for Chrome."
