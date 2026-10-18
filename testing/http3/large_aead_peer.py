"""Explicit test-peer AEAD adapter; this is not stock aioquic interop.

aioquic 1.3.0's native AEAD has a 1500-byte ciphertext receive limit. This
process-local factory uses cryptography's authenticated primitives with QUIC's
same nonce, AAD and appended 16-byte tag. Header protection is unchanged.
Never modifies installed packages. Enable only for this directed test peer.
"""
from __future__ import annotations

import hashlib
import importlib.metadata
from pathlib import Path

from aioquic._crypto import CryptoError
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305


class LargePacketAEAD:
    def __init__(self, cipher_name: bytes, key: bytes, iv: bytes):
        choices = {b"aes-128-gcm": (AESGCM, 16),
                   b"aes-256-gcm": (AESGCM, 32),
                   b"chacha20-poly1305": (ChaCha20Poly1305, 32)}
        if cipher_name not in choices:
            raise CryptoError("Invalid cipher name")
        factory, length = choices[cipher_name]
        if len(key) != length or len(iv) != 12:
            raise CryptoError("Invalid key or IV length")
        self._cipher = factory(bytes(key))
        self._iv = bytes(iv)

    def _nonce(self, packet_number: int) -> bytes:
        # The native API receives an unsigned 64-bit integer. QUIC packet
        # numbers use 62 bits; retain the underlying API's 64-bit domain.
        if not isinstance(packet_number, int) or not 0 <= packet_number < 1 << 64:
            raise OverflowError("packet number is outside unsigned 64-bit range")
        return (int.from_bytes(self._iv, "big") ^ packet_number).to_bytes(12, "big")

    def encrypt(self, data: bytes, associated_data: bytes, packet_number: int) -> bytes:
        return self._cipher.encrypt(self._nonce(packet_number), bytes(data),
                                    bytes(associated_data))

    def decrypt(self, data: bytes, associated_data: bytes, packet_number: int) -> bytes:
        try:
            return self._cipher.decrypt(self._nonce(packet_number), bytes(data),
                                        bytes(associated_data))
        except InvalidTag as error:
            raise CryptoError("Payload decryption failed") from error


def install() -> dict:
    """Replace only CryptoContext's process-local AEAD factory, explicitly."""
    import aioquic._crypto as native
    import aioquic.quic.crypto as crypto
    native_path = Path(native.__file__).resolve()
    adapter_path = Path(__file__).resolve()
    crypto.AEAD = LargePacketAEAD
    return {"kind": "cryptography-large-packet-test-peer",
            "stock_aioquic_interoperability": False,
            "aioquic_version": importlib.metadata.version("aioquic"),
            "cryptography_version": importlib.metadata.version("cryptography"),
            "original_backend": str(native_path),
            "original_backend_sha256": hashlib.sha256(native_path.read_bytes()).hexdigest(),
            "adapter_sha256": hashlib.sha256(adapter_path.read_bytes()).hexdigest(),
            "header_protection": "unchanged aioquic backend",
            "scope": "client and independent proxy CryptoContext AEAD factory"}
