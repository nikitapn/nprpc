#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# IMPORTANT: ALWAYS USE SHORT-LIVED CERTIFICATE FOR TESTING PURPOSES (13 DAYS MAX)
DAYS=13

# --if-stale: keep the certificate unless it is missing or expires within
# RENEW_BEFORE_DAYS. What run_all_tests.py calls, so a short-lived cert never
# fails the WebTransport tests just because nobody remembered to recreate it.
RENEW_BEFORE_DAYS=2

mkdir -p out

if [ "${1:-}" = "--if-stale" ]; then
  if [ -f out/localhost.crt ] && [ -f out/localhost.key ] &&
     openssl x509 -in out/localhost.crt -noout \
       -checkend $((RENEW_BEFORE_DAYS * 86400)) >/dev/null 2>&1; then
    echo "test certificate ok ($(openssl x509 -in out/localhost.crt -noout -enddate))"
    exit 0
  fi
  QUIET=1
fi

openssl req \
  -x509 \
  -newkey ec \
  -pkeyopt ec_paramgen_curve:P-256 \
  -pkeyopt ec_param_enc:named_curve \
  -sha256 \
  -days $DAYS \
  -nodes \
  -keyout out/localhost.key \
  -out out/localhost.crt \
  -subj '/CN=localhost' \
  -extensions san \
  -config <(cat << EOF
[req]
distinguished_name=req
[san]
subjectAltName=@alt_names
[alt_names]
DNS.1=localhost
DNS.2=linuxvm
DNS.3=calculator.lan
EOF
)

if [ -n "${QUIET:-}" ]; then
  echo "test certificate recreated ($(openssl x509 -in out/localhost.crt -noout -enddate))"
else
  openssl x509 -in out/localhost.crt -text -noout | sed -n '1,220p'
fi