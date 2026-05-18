#!/usr/bin/env python3
"""
generate_jwt.py - Generate an ES256 JWT for nRF Cloud API authentication.

Mirrors the firmware's custom_jwt_generate() / nrf_cloud_jwt_generate():
  - Algorithm : ES256  (ECDSA with P-256 / SHA-256)
  - iss / sub : client_id  (device ID, e.g. "<imei>")
  - iat       : current UTC epoch seconds
  - exp       : iat + validity_s
  - Signature : raw R||S (IEEE P1363, 64 bytes), not DER

Usage:
    python3 generate_jwt.py --private-key path/to/private.pem --client-id <imei>
    python3 generate_jwt.py --private-key path/to/private.pem --client-id <imei> --validity 3600

Requirements:
    pip install cryptography
"""

import argparse
import base64
import json
import sys
import time


def generate_jwt(
    private_key_pem: str,
    client_id: str,
    validity_s: int = 600,
) -> str:
    """Generate an ES256 JWT for nRF Cloud API authentication.

    Mirrors the firmware's custom_jwt_generate():
      - Algorithm : ES256  (ECDSA with P-256 / SHA-256)
      - iss / sub : client_id  (device ID, e.g. "<imei>")
      - iat       : current UTC epoch seconds
      - exp       : iat + validity_s
      - Signature : raw R||S (IEEE P1363, 64 bytes), not DER

    Args:
        private_key_pem: PEM string of the EC P-256 private key.
        client_id:       nRF Cloud device ID (used for both iss and sub).
        validity_s:      Token lifetime in seconds (default 600 = 10 min).

    Returns:
        Compact JWT string suitable for use as a Bearer token.

    Raises:
        ImportError: if the ``cryptography`` package is not installed.
        ValueError:  if the key is not an EC P-256 key.
    """
    try:
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    except ImportError:
        print(
            "The 'cryptography' package is required for JWT generation.\n"
            "Install with: pip install cryptography",
            file=sys.stderr,
        )
        sys.exit(1)

    def _b64url(data: bytes) -> str:
        return base64.urlsafe_b64encode(data).rstrip(b"=").decode()

    key = serialization.load_pem_private_key(private_key_pem.encode(), password=None)
    if not isinstance(key, ec.EllipticCurvePrivateKey):
        raise ValueError("private_key_pem must be an EC private key")
    if not isinstance(key.curve, ec.SECP256R1):
        raise ValueError(f"Expected P-256 (secp256r1) key, got {key.curve.name}")

    header  = _b64url(json.dumps({"alg": "ES256", "typ": "JWT", "kid": client_id}, separators=(",", ":")).encode())
    now     = int(time.time())
    payload = _b64url(json.dumps(
        {"iss": client_id, "sub": client_id, "iat": now, "exp": now + validity_s},
        separators=(",", ":"),
    ).encode())

    signing_input = f"{header}.{payload}".encode()

    # ECDSA returns DER-encoded signature; JWT requires raw R||S (32 bytes each)
    der_sig = key.sign(signing_input, ec.ECDSA(hashes.SHA256()))
    r, s    = decode_dss_signature(der_sig)
    raw_sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    return f"{signing_input.decode()}.{_b64url(raw_sig)}"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate an ES256 JWT for nRF Cloud API authentication."
    )
    parser.add_argument(
        "--private-key", required=True, metavar="PATH",
        help="Path to EC P-256 private key PEM file (same key provisioned on the device)",
    )
    parser.add_argument(
        "--client-id", required=True, metavar="DEVICE_ID",
        help="nRF Cloud device ID, e.g. <imei> (used as iss and sub in the JWT)",
    )
    parser.add_argument(
        "--validity", type=int, default=600, metavar="SECONDS",
        help="JWT lifetime in seconds (default: 600 = 10 min)",
    )
    args = parser.parse_args()

    try:
        with open(args.private_key, "r", encoding="utf-8") as f:
            pem = f.read()
    except OSError as e:
        print(f"Cannot read private key file: {e}", file=sys.stderr)
        sys.exit(1)

    token = generate_jwt(pem, args.client_id, args.validity)

    print(token)

    # Decode and print claims for inspection
    try:
        parts = token.split(".")

        def _pad(s: str) -> str:
            return s + "=" * (-len(s) % 4)

        header_json  = base64.urlsafe_b64decode(_pad(parts[0])).decode()
        payload_json = base64.urlsafe_b64decode(_pad(parts[1])).decode()
        print(f"\nHeader:  {header_json}", file=sys.stderr)
        print(f"Payload: {payload_json}", file=sys.stderr)
        print(f"\nDecode at: https://jwt.io", file=sys.stderr)
    except Exception as e:
        print(f"(could not decode JWT for display: {e})", file=sys.stderr)


if __name__ == "__main__":
    main()
