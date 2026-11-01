#!/usr/bin/env python3
"""npquicrouter's limits, read back through its [stats] log line."""
import json
import os
import pathlib
import re
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time

try:
    from cryptography.hazmat.primitives import hashes, hmac
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:
    print('router_limits requires Python cryptography')
    sys.exit(77)

fixture, router, cert, key = sys.argv[1:5]
TCP_PORT, UDP_PORT, BACKEND_PORT = 28481, 28482, 28483
QUIC_V1_SALT = bytes.fromhex('38762cf7f55934b34d179ae6a4c80cadccbb7f0a')


def hkdf_extract(salt, ikm):
    mac = hmac.HMAC(salt, hashes.SHA256())
    mac.update(ikm)
    return mac.finalize()


def hkdf_expand_label(secret, label, length):
    full = b'tls13 ' + label
    info = struct.pack('>HB', length, len(full)) + full + b'\0'
    mac = hmac.HMAC(secret, hashes.SHA256())
    mac.update(info + b'\x01')
    return mac.finalize()[:length]


def varint(value):
    return struct.pack('>H', 0x4000 | value) if value >= 64 else bytes([value])


def initial_without_sni(dcid):
    """A genuine client Initial whose CRYPTO data starts past offset 0, so the
    router can decrypt it but has to hold it waiting for the rest."""
    client = hkdf_expand_label(hkdf_extract(QUIC_V1_SALT, dcid), b'client in', 32)
    aead_key = hkdf_expand_label(client, b'quic key', 16)
    iv = hkdf_expand_label(client, b'quic iv', 12)
    hp = hkdf_expand_label(client, b'quic hp', 16)

    payload = b'\x06' + varint(100) + varint(8) + b'x' * 8
    payload += b'\0' * (1200 - len(payload))
    number = b'\0\0\0\0'
    length = struct.pack('>H', 0x4000 | (len(payload) + 16 + len(number)))
    header = bytes([0xC3]) + struct.pack('>I', 1) + bytes([len(dcid)]) + dcid + b'\0' + b'\0' + length
    sealed = AESGCM(aead_key).encrypt(iv, payload, header + number)

    sample = sealed[0:16]  # Starts 4 bytes after the packet number's first byte.
    encryptor = Cipher(algorithms.AES(hp), modes.ECB()).encryptor()
    mask = encryptor.update(sample) + encryptor.finalize()
    protected_first = bytes([header[0] ^ (mask[0] & 0x0F)])
    protected_number = bytes(a ^ b for a, b in zip(number, mask[1:5]))
    return protected_first + header[1:] + protected_number + sealed


class Router:
    def __init__(self, root, backend, **limits):
        self.log = root / f'router-{time.monotonic_ns()}.log'
        config = root / 'router.json'
        config.write_text(json.dumps({
            'listen_address': '127.0.0.1', 'listen_tcp_port': TCP_PORT, 'listen_udp_port': UDP_PORT,
            'http_redirect_port': 0, 'acme_webroot': '', 'default_tcp_backend': f'127.0.0.1:{BACKEND_PORT}',
            'default_udp_backend': '', 'num_workers': 1, 'stats_interval_sec': 1,
            'routes': [{'sni': 'localhost', 'udp_backend': f'127.0.0.1:{backend}',
                        'tcp_backend': f'127.0.0.1:{BACKEND_PORT}'}], **limits}))
        self.process = subprocess.Popen([router, str(config)], stdout=subprocess.DEVNULL,
                                        stderr=self.log.open('w'))
        time.sleep(0.3)

    def stats(self):
        """Waits for a [stats] line written from now on, so it covers what came before."""
        seen = self.log.read_text().count('[stats]')
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            lines = [l for l in self.log.read_text().splitlines() if l.startswith('[stats]')]
            if len(lines) > seen:
                return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)(?: |$)', lines[-1])} | {'raw': lines[-1]}
            time.sleep(0.05)
        raise AssertionError('no [stats] line')

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.process.kill()
        self.process.wait()


def udp_send(*packets):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for packet in packets:
        sock.sendto(packet, ('127.0.0.1', UDP_PORT))
    sock.close()
    time.sleep(0.1)


def curl_ok(timeout='5'):
    return subprocess.run(
        ['curl', '-sk', '--http3-only', '-m', timeout, '--resolve', f'localhost:{UDP_PORT}:127.0.0.1',
         f'https://localhost:{UDP_PORT}/payload', '-o', os.devnull], capture_output=True).returncode == 0


def expect(label, condition, stats):
    if not condition:
        raise AssertionError(f'{label}\n{stats["raw"]}')
    print('PASS:', label, flush=True)


curl_version = subprocess.run(['curl', '--version'], capture_output=True, text=True)
has_http3 = curl_version.returncode == 0 and 'HTTP3' in curl_version.stdout

with tempfile.TemporaryDirectory(prefix='nprpc-router-limits-') as directory:
    root = pathlib.Path(directory)

    with Router(root, 9) as r:
        udp_send(b'\xC0' + b'\0' * 99, b'\xC0' + os.urandom(1199), b'\x40' + os.urandom(100))
        s = r.stats()
        expect('an undersized datagram is dropped before decrypting', s['initial_too_small'] == 1, s)
        expect('a datagram that does not decrypt is counted', s['initial_undecryptable'] == 1, s)
        expect('an unknown short header is counted', s['unknown_short_header'] == 1, s)

    with Router(root, 9, udp_new_initials_per_ip_per_second=5) as r:
        udp_send(*[initial_without_sni(os.urandom(8)) for _ in range(100)])
        s = r.stats()
        expect('new connections are rate limited per IP', s['initial_rate_limited'] >= 80 and s['pending'] <= 20, s)

    with Router(root, 9, udp_new_initials_per_second=5, udp_new_initials_per_ip_per_second=0) as r:
        udp_send(*[initial_without_sni(os.urandom(8)) for _ in range(100)])
        s = r.stats()
        expect('new connections are rate limited overall', s['initial_rate_limited'] >= 80, s)

    with Router(root, 9, udp_max_pending_per_ip=3, udp_handshake_timeout_sec=1) as r:
        udp_send(*[initial_without_sni(os.urandom(8)) for _ in range(20)])
        s = r.stats()
        expect('pending connections are capped per IP', s['pending'] == 3 and s['pending_cap_drops'] == 17, s)
        time.sleep(6)  # The 5 second pending timeout.
        s = r.stats()
        expect('an expired pending connection gives its place back', s['pending'] == 0, s)
        expect('a connection that never showed an SNI is counted', s['initial_no_sni'] == 3, s)

    with Router(root, 9, udp_max_pending=2, udp_max_pending_per_ip=0) as r:
        udp_send(*[initial_without_sni(os.urandom(8)) for _ in range(20)])
        s = r.stats()
        expect('pending connections are capped overall', s['pending'] == 2 and s['pending_cap_drops'] == 18, s)

    with Router(root, 9, tcp_max_connections_per_ip=2, tcp_peek_timeout_sec=1) as r:
        clients = [socket.create_connection(('127.0.0.1', TCP_PORT)) for _ in range(3)]
        time.sleep(0.2)
        s = r.stats()
        expect('TCP connections are capped per IP', s['tcp'] == 2 and s['tcp_cap_drops'] == 1, s)
        time.sleep(1.5)
        s = r.stats()
        expect('a connection that sends no ClientHello times out', s['tcp'] == 0 and s['tcp_peek_timeouts'] == 2, s)
        for client in clients:
            client.close()

    if not has_http3:
        print('SKIP: real connections need curl with HTTP/3', flush=True)
        sys.exit(0)

    # Nothing listens on the backend port, so these handshakes never finish.
    with Router(root, 9, udp_handshake_timeout_sec=1) as r:
        curl_ok('1')
        s = r.stats()
        expect('a session is created for a new client', s['sessions'] == 1 or s['handshake_timeouts'] == 1, s)
        time.sleep(3)
        s = r.stats()
        # The gauge only falls when the session is really destroyed, which a
        # pending receive on its backend socket used to prevent.
        expect('a session that never finishes its handshake is destroyed',
               s['sessions'] == 0 and s['handshake_timeouts'] == 1, s)

    # Nothing reads this ring, so what the router writes stays in it.
    channel = f'router_limits_{os.getpid()}'
    routes = [{'sni': 'localhost', 'udp_backend': f'127.0.0.1:{BACKEND_PORT}', 'shm_ingress_channel': channel}]
    try:
        with Router(root, BACKEND_PORT, routes=routes) as r:
            curl_ok('1')
            s = r.stats()
            used = re.search(rf'ingress\[[^\]]*{channel}[^\]]*\]=(\d+)/(\d+)B', s['raw'])
            expect('an unread ring shows what is waiting in it',
                   used is not None and 0 < int(used[1]) < int(used[2]), s)
    finally:
        pathlib.Path(f'/dev/shm/nprpc_{channel}_c2s').unlink(missing_ok=True)

    www = root / 'www'
    www.mkdir()
    (www / 'payload').write_bytes(b'x' * 100_000)
    server = subprocess.Popen([fixture, cert, key, str(www), str(BACKEND_PORT)],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        if not select.select([server.stdout], [], [], 10)[0] or server.stdout.readline().strip() != 'ready':
            raise AssertionError('server did not start')
        with Router(root, BACKEND_PORT, udp_max_sessions_per_ip=1, udp_handshake_timeout_sec=1) as r:
            ok = curl_ok()
            time.sleep(2.5)
            s = r.stats()
            expect('a real client gets through the limits', ok, s)
            expect('an established session outlives the handshake timeout',
                   s['sessions'] == 1 and s['handshake_timeouts'] == 0, s)
            expect('traffic is counted per backend, both ways',
                   re.search(rf'backend\[127\.0\.0\.1:{BACKEND_PORT}\]=[1-9]\d*pkts/\d+B/0ring_full/[1-9]\d*reply_pkts/[1-9]\d*reply_B',
                             s['raw']) is not None, s)
            ok = curl_ok('1')
            s = r.stats()
            expect('sessions are capped per IP', not ok and s['session_cap_drops'] >= 1 and s['sessions'] == 1, s)
    finally:
        server.kill()
        server.wait()
