import fs from 'fs';
import https from 'https';
import path from 'path';
import { createHash, X509Certificate } from 'crypto';

import { Browser, chromium } from 'playwright-core';
import { describe, it, before, after } from 'mocha';
import { expect } from 'chai';

import { EndPointType } from 'nprpc';
import { ServerManager } from './server-manager';

function getServerCertificateHash(certPath: string): number[] {
  const certificate = new X509Certificate(fs.readFileSync(certPath));
  return Array.from(createHash('sha256').update(certificate.raw).digest());
}

describe('WebTransport Browser Transport', function() {
  this.timeout(60000);

  const serverManager = new ServerManager();
  const chromiumExecutable = process.env.CHROMIUM_BIN || '/usr/bin/chromium';
  const runtimeBundlePath = path.resolve(process.cwd(), '../../nprpc_js/dist/index.js');
  const certificatePath = path.resolve(process.cwd(), '../../certs/out/localhost.crt');
  const certificateKeyPath = path.resolve(process.cwd(), '../../certs/out/localhost.key');
  const browserFixturePort = 24443;

  let browser: Browser | null = null;
  let fixtureServer: https.Server | null = null;

  before(async function() {
    if (!fs.existsSync(chromiumExecutable)) {
      this.skip();
    }

    if (!fs.existsSync(runtimeBundlePath)) {
      throw new Error(`Browser runtime bundle not found: ${runtimeBundlePath}`);
    }

    if (!fs.existsSync(certificatePath)) {
      throw new Error(`Server certificate not found: ${certificatePath}`);
    }

    if (!fs.existsSync(certificateKeyPath)) {
      throw new Error(`Server certificate key not found: ${certificateKeyPath}`);
    }

    const started = await serverManager.startAll();
    if (!started) {
      throw new Error('Failed to start NPRPC integration server');
    }

    fixtureServer = https.createServer(
      {
        cert: fs.readFileSync(certificatePath),
        key: fs.readFileSync(certificateKeyPath),
      },
      (_req, res) => {
        res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
        res.end('<!doctype html><html><body>webtransport fixture</body></html>');
      },
    );

    await new Promise<void>((resolve, reject) => {
      fixtureServer!.once('error', reject);
      fixtureServer!.listen(browserFixturePort, '127.0.0.1', () => resolve());
    });

    browser = await chromium.launch({
      executablePath: chromiumExecutable,
      headless: true,
      args: [
        '--enable-quic',
        '--allow-insecure-localhost',
        '--ignore-certificate-errors',
        '--origin-to-force-quic-on=localhost:22223',
      ],
    });
  });

  after(async function() {
    await browser?.close();
    await new Promise<void>((resolve) => fixtureServer?.close(() => resolve()));
    serverManager.killServer();
  });

  it('should establish a WebTransport session to the HTTP/3 test server in headless Chromium', async function() {
    if (!browser) {
      this.skip();
    }

    const context = await browser.newContext({
      ignoreHTTPSErrors: true,
    });

    try {
      const page = await context.newPage();
      await page.goto(`https://localhost:${browserFixturePort}/`, { waitUntil: 'load' });
      await page.addScriptTag({ path: runtimeBundlePath });

      const certificateHash = getServerCertificateHash(certificatePath);
      const result = await page.evaluate(async ({ hashBytes }) => {
        const NPRPC = (globalThis as any).nprpc_runtime;
        if (!NPRPC) {
          return { error: 'nprpc runtime bundle was not injected' };
        }

        if (typeof (globalThis as any).WebTransport === 'undefined') {
          return { unsupported: true };
        }

        const secureContext = globalThis.isSecureContext;

        await NPRPC.init(false, {
          secured: true,
          webtransport: true,
          webtransport_options: {
            serverCertificateHashes: [
              {
                algorithm: 'sha-256',
                value: new Uint8Array(hashBytes),
              },
            ],
          },
          objects: {},
        });

        const endpoint = new NPRPC.EndPoint(NPRPC.EndPointType.WebTransport, 'localhost', 22223);
        const connection = new NPRPC.Connection(endpoint);
        await connection.ready_;

        const closedState = await Promise.race([
          connection.wt.closed.then(
            () => 'closed',
            () => 'closed',
          ),
          new Promise((resolve) => setTimeout(() => resolve('open'), 250)),
        ]);

        return {
          unsupported: false,
          secureContext,
          endpointType: connection.endpoint.type,
          closedState,
        };
      }, { hashBytes: certificateHash });

      if ((result as any).unsupported) {
        this.skip();
      }

      expect((result as any).error).to.equal(undefined);
      expect((result as any).secureContext).to.equal(true);
      expect((result as any).endpointType).to.equal(EndPointType.WebTransport);
      expect((result as any).closedState).to.equal('open');
    } finally {
      await context.close();
    }
  });
});