#!/bin/bash

# IMPORTANT: ALWAYS USE SHORT-LIVED CERTIFICATE FOR TESTING PURPOSES (13 DAYS MAX)
# TO AVOID SECURITY ISSUES AND COMPATIBILITY PROBLEMS WITH BROWSERS.
# AND DON'T FORGET TO REINSTALL IT MANUALLY IN THE CHROMIUM AFTER REGENERATING!!!
DAYS=13

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
EOF
)