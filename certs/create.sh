#!/bin/bash

openssl req \
  -x509 \
  -newkey rsa:4096 \
  -sha256 \
  -days 3560 \
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