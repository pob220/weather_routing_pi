"""Reject mixed host/plugin TLS stacks in Android plugin packages."""
import re
import subprocess
import sys

readelf, library = sys.argv[1:]
symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", library], text=True)
private = re.compile(r"^(curl_|SSL_|OPENSSL_|EVP_|OBJ_|ASN1_|CRYPTO_|X509_|BIO_|OSSL_|ERR_|HMAC_|PEM_|BN_|RSA_|EC_|ECDSA_)")
leaks = []
for line in symbols.splitlines():
    columns = line.split()
    if len(columns) >= 8 and private.match(columns[7]):
        leaks.append(line.strip())
if leaks:
    raise SystemExit("Android TLS symbols must be defined privately, never imported from/exported to OpenCPN:\n" + "\n".join(leaks))
print("Android TLS isolation verified: no host imports or public exports")
