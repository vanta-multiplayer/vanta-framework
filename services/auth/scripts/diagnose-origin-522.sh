#!/usr/bin/env bash
# Diagnose Cloudflare 522 (origin unreachable) while staying orange-clouded.
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== docker ==="
docker compose ps || true

echo
echo "=== listening on 80/443 ==="
ss -tlnp 2>/dev/null | grep -E ':80 |:443 ' || netstat -tlnp 2>/dev/null | grep -E ':80 |:443 ' || true

echo
echo "=== local origin (must work) ==="
echo -n "http  : "; curl -sS -o /dev/null -w '%{http_code}\n' --max-time 5 http://127.0.0.1/ -H 'Host: id.vantamp.xyz' || echo FAIL
echo -n "https : "; curl -sk -o /dev/null -w '%{http_code}\n' --max-time 5 https://127.0.0.1/health -H 'Host: id.vantamp.xyz' || echo FAIL

echo
echo "=== this VPS public IPs (must match Cloudflare DNS A / AAAA for id.vantamp.xyz) ==="
echo -n "IPv4: "; curl -4 -sS --max-time 5 https://1.1.1.1/cdn-cgi/trace 2>/dev/null | awk -F= '/^ip=/{print $2}' || curl -4 -sS --max-time 5 ifconfig.me || echo unknown
echo -n "IPv6: "; curl -6 -sS --max-time 5 https://[2606:4700:4700::1111]/cdn-cgi/trace 2>/dev/null | awk -F= '/^ip=/{print $2}' || echo "(none/unreachable)"

echo
echo "=== Cloudflare resolves id.vantamp.xyz to (proxied CF anycast — ok) ==="
getent ahostsv4 id.vantamp.xyz 2>/dev/null | head -3 || true

echo
echo "=== firewall (ufw) ==="
if command -v ufw >/dev/null 2>&1; then
  ufw status verbose || true
else
  echo "(ufw not installed)"
fi

echo
echo "=== what Cloudflare needs ==="
cat <<'EOF'
1) Cloudflare DNS A record for id.vantamp.xyz = this VPS IPv4 above (orange OK).
   If an AAAA exists, it must be THIS machine's IPv6 or delete AAAA
   (LE/CF often prefer AAAA — wrong AAAA ⇒ 522).
2) Host/provider firewall must allow Cloudflare → TCP 443 (and 80):
     ufw allow 80/tcp
     ufw allow 443/tcp
     ufw reload
   Or allow only CF ranges: https://www.cloudflare.com/ips/
3) SSL/TLS mode: Full (self-signed) or Full strict (Origin CA).
4) Do NOT run certs:http for Epic when orange-clouded — public cert is Cloudflare's.
EOF
