#!/bin/bash

openssl req \
  -x509 \
  -newkey ec \
  -pkeyopt ec_paramgen_curve:P-256 \
  -pkeyopt ec_param_enc:named_curve \
  -sha256 \
  -days 13 \
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