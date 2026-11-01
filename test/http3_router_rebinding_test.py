#!/usr/bin/env python3
"""HTTP/3 downloads that must survive a NAT rebinding, direct and through npquicrouter."""
import hashlib
import json
import os
import pathlib
import re
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time

FILE_SIZE = 12_000_000
RATE = '8M'  # Long enough that the port change lands mid-transfer.
REBIND_AFTER = 0.5
SERVER_PORT = 28470
ROUTED_SERVER_PORT = 28460
ROUTER_PORT = 28451

fixture, router, cert, key = sys.argv[1:5]

curl_version = subprocess.run(['curl', '--version'], capture_output=True, text=True)
if curl_version.returncode != 0 or 'HTTP3' not in curl_version.stdout:
    print('http3_router_rebinding requires curl with HTTP/3')
    sys.exit(77)


def start_relay(target, rebind_after):
    """Forwards to target, moving to a new source port as a NAT rebinding would."""
    downstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    downstream.bind(('127.0.0.1', 0))
    stop = threading.Event()
    moved = {'replies': 0}

    def run():
        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client = None
        rebind_at = time.monotonic() + rebind_after if rebind_after else None
        rebound = False
        while not stop.is_set():
            if rebind_at and time.monotonic() >= rebind_at:
                upstream.close()
                upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                rebind_at = None
                rebound = True
            for ready in select.select([downstream, upstream], [], [], 0.02)[0]:
                data, sender = ready.recvfrom(65535)
                if ready is downstream:
                    client = sender
                    upstream.sendto(data, ('127.0.0.1', target))
                elif client:
                    downstream.sendto(data, client)
                    moved['replies'] += rebound
        upstream.close()
        downstream.close()

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    return downstream.getsockname()[1], stop, moved, thread


def download(label, root, target, rebind_after, expected):
    port, stop, moved, thread = start_relay(target, rebind_after)
    out = root / 'downloaded'
    out.unlink(missing_ok=True)
    result = subprocess.run(
        ['curl', '-sk', '--http3-only', '--limit-rate', RATE, '-m', '15',
         '--resolve', f'localhost:{port}:127.0.0.1',
         f'https://localhost:{port}/payload', '-o', str(out)],
        capture_output=True, text=True)
    stop.set()
    thread.join()
    got = hashlib.sha256(out.read_bytes()).hexdigest() if out.exists() else None
    if result.returncode != 0 or got != expected:
        raise AssertionError(f'{label}: curl={result.returncode}, '
                             f'{out.stat().st_size if out.exists() else 0} of {FILE_SIZE} bytes')
    if rebind_after and moved['replies'] == 0:
        raise AssertionError(f'{label}: finished before the port changed, which shows nothing')
    print('PASS:', label, flush=True)


with tempfile.TemporaryDirectory(prefix='nprpc-http3-rebinding-') as directory:
    root = pathlib.Path(directory)
    www = root / 'www'
    www.mkdir()
    payload = os.urandom(FILE_SIZE)
    (www / 'payload').write_bytes(payload)
    expected = hashlib.sha256(payload).hexdigest()

    egress = f'rebinding_{os.getpid()}_s'
    ingress = f'rebinding_{os.getpid()}_c'
    (root / 'router.json').write_text(json.dumps({
        'listen_address': '127.0.0.1', 'listen_tcp_port': ROUTER_PORT - 1, 'listen_udp_port': ROUTER_PORT,
        'http_redirect_port': 0, 'acme_webroot': '', 'default_tcp_backend': '', 'default_udp_backend': '',
        'udp_session_timeout_sec': 120, 'shm_egress_channel': egress, 'num_workers': 1, 'stats_interval_sec': 1,
        'routes': [{'sni': 'localhost', 'udp_backend': f'127.0.0.1:{ROUTED_SERVER_PORT}',
                    'shm_ingress_channel': ingress}],
    }))

    def start_server(*args):
        server = subprocess.Popen([fixture, cert, key, str(www), *args],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        if not select.select([server.stdout], [], [], 10)[0] or server.stdout.readline().strip() != 'ready':
            raise AssertionError('server did not start')
        return server

    processes = []
    try:
        # Creates the rings the routed server opens.
        router_log = root / 'router.log'
        processes.append(subprocess.Popen([router, str(root / 'router.json')],
                                          stdout=subprocess.DEVNULL, stderr=router_log.open('w')))
        time.sleep(0.5)
        processes.append(start_server(str(ROUTED_SERVER_PORT), egress, ingress))
        processes.append(start_server(str(SERVER_PORT)))
        time.sleep(0.5)

        download('through the router, no port change', root, ROUTER_PORT, None, expected)
        download('through the router, port change', root, ROUTER_PORT, REBIND_AFTER, expected)

        # At least the file went out through the egress ring.
        time.sleep(1.5)
        stats = [l for l in router_log.read_text().splitlines() if l.startswith('[stats]')][-1]
        sent = re.search(rf'egress\[[^\]]*{egress}[^\]]*\]=\d+/\d+B/([1-9]\d*)pkts/(\d+)B/0send_errors', stats)
        if not sent or int(sent[2]) < FILE_SIZE:
            raise AssertionError(f'egress traffic is not counted\n{stats}')
        print('PASS: egress traffic is counted', flush=True)

        download('direct, port change', root, SERVER_PORT, REBIND_AFTER, expected)
    finally:
        for process in processes:
            process.kill()
            process.wait()
        for channel, suffix in ((egress, 's2c'), (ingress, 'c2s')):
            pathlib.Path(f'/dev/shm/nprpc_{channel}_{suffix}').unlink(missing_ok=True)
