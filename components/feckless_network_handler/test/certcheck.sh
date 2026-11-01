#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Puts OpenSSL to what certtest made in $1: the certificate parses, names
# what feckless_cert.h says it names, is valid for the whole span, is
# self-signed and verifies against itself, belongs to the key beside it,
# and has the fingerprint feckless_cert_fingerprint() computed.
set -eu
d=$1
fail=0
ok()  { echo "ok   $1"; }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }

crt=$d/crt.pem
text=$(openssl x509 -in "$crt" -noout -text)

echo "$text" | grep -q "Subject: CN *= *Feckless-A1B2C3, O *= *Feckless" && ok subject || bad subject
echo "$text" | grep -q "Issuer: CN *= *Feckless-A1B2C3, O *= *Feckless" && ok "issuer is the subject" || bad issuer
echo "$text" | grep -q "DNS:feckless-a1b2c3.local" && ok "SAN is the lower-case name.local" || bad SAN
echo "$text" | grep -q "IP Address" && bad "no IP in the SAN" || ok "no IP in the SAN"
echo "$text" | grep -q "CA:FALSE" && ok "CA:FALSE" || bad CA
echo "$text" | grep -q "Digital Signature" && ok "keyUsage digitalSignature" || bad keyUsage
echo "$text" | grep -q "ecdsa-with-SHA256" && ok "ecdsa-with-SHA256" || bad sigalg
echo "$text" | grep -q "prime256v1" && ok "P-256" || bad curve
echo "$text" | grep -q "Version: 3" && ok "v3" || bad version
echo "$text" | grep -q "Not Before: Jan  1 00:00:00 2026" && ok "valid from 2026-01-01" || bad notBefore
echo "$text" | grep -q "Not After : Dec 31 23:59:59 2049" && ok "valid to 2049-12-31" || bad notAfter

openssl verify -CAfile "$crt" "$crt" >/dev/null 2>&1 && ok "verifies against itself" || bad "self-verify"

kp=$(openssl pkey -in "$d/key.pem" -pubout 2>/dev/null | openssl sha256)
cp=$(openssl x509 -in "$crt" -noout -pubkey | openssl sha256)
[ "$kp" = "$cp" ] && ok "key belongs to the certificate" || bad "key/certificate pair"

want=$(openssl x509 -in "$crt" -noout -fingerprint -sha256 | sed 's/^.*=//')
got=$(cat "$d/fp.txt")
[ "$want" = "$got" ] && ok "fingerprint matches OpenSSL's" || bad "fingerprint ($got vs $want)"

serial=$(openssl x509 -in "$crt" -noout -serial | sed 's/serial=//')
[ ${#serial} -eq 32 ] && ok "16-byte serial" || bad "serial length ${#serial}"
case $serial in [0-7]*) ok "serial positive";; *) bad "serial top bit";; esac

echo "certcheck: $fail failed"
exit $fail
