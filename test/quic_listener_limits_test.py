#!/usr/bin/env python3
"""The native QUIC listener's defences, from a raw socket and a real client."""
import os
import select
import socket
import struct
import subprocess
import sys
import time

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from cryptography.hazmat.primitives.hashes import SHA256
    from cryptography.hazmat.primitives.hmac import HMAC
except ImportError:
    print('This test needs Python cryptography')
    sys.exit(77)

fixture, certificate, key = sys.argv[1:4]
PORT = 28444
QUIC_V1_SALT = bytes.fromhex('38762cf7f55934b34d179ae6a4c80cadccbb7f0a')


def hmac(salt, data):
    h = HMAC(salt, SHA256())
    h.update(data)
    return h.finalize()


def expand_label(secret, label, length):
    info = struct.pack('>H', length) + bytes([len(b'tls13 ' + label)]) + b'tls13 ' + label + b'\0'
    return hmac(secret, info + b'\x01')[:length]


def varint(value):
    return struct.pack('>H', 0x4000 | value) if value >= 64 else bytes([value])


def initial(dcid, scid, token=b''):
    """A client Initial holding the start of a ClientHello, which is what a client
    that stalls mid-handshake sends: the TLS stack waits for the rest."""
    client = expand_label(hmac(QUIC_V1_SALT, dcid), b'client in', 32)
    aead_key = expand_label(client, b'quic key', 16)
    iv = expand_label(client, b'quic iv', 12)
    hp = expand_label(client, b'quic hp', 16)
    hello = b'\x01\x00\x03\xe8' + b'\0' * 16  # Promises 1000 bytes, and has 16.
    payload = b'\x06\x00' + varint(len(hello)) + hello
    payload += b'\0' * (1200 - len(payload))
    number = b'\0\0\0\0'
    length = struct.pack('>H', 0x4000 | (len(payload) + 16 + len(number)))
    header = (bytes([0xC3]) + struct.pack('>I', 1) + bytes([len(dcid)]) + dcid
              + bytes([len(scid)]) + scid + varint(len(token)) + token + length)
    sealed = AESGCM(aead_key).encrypt(iv, payload, header + number)
    encryptor = Cipher(algorithms.AES(hp), modes.ECB()).encryptor()
    mask = encryptor.update(sealed[:16]) + encryptor.finalize()
    return (bytes([header[0] ^ (mask[0] & 0x0F)]) + header[1:]
            + bytes(a ^ b for a, b in zip(number, mask[1:5])) + sealed)


def parse_retry(packet):
    """(the server's new connection ID, the token)"""
    if packet[0] & 0xF0 != 0xF0:
        raise AssertionError(f'not a Retry: first byte {packet[0]:#x}')
    offset = 5
    offset += 1 + packet[offset]  # Echoes our source connection ID as its destination.
    length = packet[offset]
    scid = packet[offset + 1:offset + 1 + length]
    return scid, packet[offset + 1 + length:-16]


def expect(label, ok, detail=''):
    if not ok:
        raise AssertionError(f'{label} {detail}')
    print('PASS:', label, flush=True)


class Server:
    def __init__(self, limits):
        self.process = subprocess.Popen([fixture, 'server', certificate, key, '-'],
                                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                        env={**os.environ, 'QUIC_TLS_LIMITS': limits})
        if not select.select([self.process.stdout], [], [], 10)[0]:
            raise AssertionError('server did not start')
        self.reference = self.process.stdout.readline().strip()
        if not self.reference.startswith('NPRPC1:'):
            raise AssertionError('no reference: ' + self.reference)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.process.terminate()
        try:
            self.process.wait(5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()

    def client_connects(self):
        result = subprocess.run([fixture, 'client', self.reference, certificate, '-', '-'],
                                capture_output=True, text=True, timeout=30)
        return result.returncode == 0


def udp():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.connect(('127.0.0.1', PORT))
    sock.settimeout(1)
    return sock


def request_retry(sock):
    dcid, scid = os.urandom(8), os.urandom(8)
    sock.send(initial(dcid, scid))
    return scid, parse_retry(sock.recv(2048))


def silent(sock):
    try:
        sock.recv(2048)
    except TimeoutError:
        return True
    return False


# A real client connects through Retry at all.
with Server('0,0,0') as server:
    expect('a real client gets through Retry', server.client_connects())

    with udp() as sock:
        dcid, scid = os.urandom(8), os.urandom(8)
        request = initial(dcid, scid)
        sock.send(request)
        reply = sock.recv(2048)
        retry_scid, token = parse_retry(reply)
        expect('an Initial with no token gets a Retry, and no more than it sent',
               len(token) > 0 and len(reply) < len(request))
        expect('that is the only reply', silent(sock))

        # Tampered: the token fails its check.
        bad = bytearray(token)
        bad[-1] ^= 1
        sock.send(initial(retry_scid, scid, bytes(bad)))
        expect('a tampered token is ignored', silent(sock))

    # A token only works from the address it was issued to.
    with udp() as first, udp() as second:
        scid, (retry_scid, token) = request_retry(first)
        second.send(initial(retry_scid, scid, token))
        expect('a token from another address is ignored', silent(second))

    # Not the Retry form: treated as having no token.
    with udp() as sock:
        sock.send(initial(os.urandom(8), os.urandom(8), b'\x36' + os.urandom(32)))
        parse_retry(sock.recv(2048))
        print('PASS: an unknown token gets a fresh Retry', flush=True)

# Clients that prove their address, then stop, use up their allowance until the
# handshake timeout frees it. No one else is affected, since they're counted per IP.
with Server('0,2,0') as server:
    sockets = [udp(), udp()]
    for sock in sockets:
        scid, (retry_scid, token) = request_retry(sock)
        sock.send(initial(retry_scid, scid, token))
    time.sleep(0.5)
    started = time.monotonic()
    expect('connections past the per-IP limit are dropped', not server.client_connects())
    remaining = 12 - (time.monotonic() - started)
    if remaining > 0:
        time.sleep(remaining)
    expect('the handshake timeout frees a stalled connection\'s slot', server.client_connects())
    for sock in sockets:
        sock.close()

with Server('1,0,0') as server:
    with udp() as sock:
        scid, (retry_scid, token) = request_retry(sock)
        sock.send(initial(retry_scid, scid, token))
        time.sleep(0.5)
        expect('connections past the overall limit are dropped', not server.client_connects())

# Over the rate, a client is dropped until the next second, when a retransmit gets in.
def timed_connect(server):
    started = time.monotonic()
    connected = server.client_connects()
    return connected, time.monotonic() - started


with Server('0,0,1') as server:
    connected, unhindered = timed_connect(server)
    expect('a client within the rate connects', connected)
    time.sleep(1.1)
    with udp() as sock:
        scid, (retry_scid, token) = request_retry(sock)
        sock.send(initial(retry_scid, scid, token))
        connected, delayed = timed_connect(server)
    expect('a client over the rate is held back until the next second',
           connected and delayed > unhindered + 0.4, f'{unhindered:.3f}s, then {delayed:.3f}s')
