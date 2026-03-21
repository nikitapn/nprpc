import fs from 'fs';
import https from 'https';
import path from 'path';
import { createHash, X509Certificate } from 'crypto';
import { execFileSync } from 'child_process';

import { Browser, chromium } from 'playwright-core';
import { describe, it, before, after } from 'mocha';
import { expect } from 'chai';

import { EndPointType } from 'nprpc';

function getServerCertificateHash(certPath: string): number[] {
  const certificate = new X509Certificate(fs.readFileSync(certPath));
  return Array.from(createHash('sha256').update(certificate.raw).digest());
}

function getServerCertificateSpki(certPath: string): string {
  const certificate = new X509Certificate(fs.readFileSync(certPath));
  const spki = certificate.publicKey.export({
    type: 'spki',
    format: 'der',
  });

  return createHash('sha256').update(spki).digest('base64');
}

function ensureValidServerCertificate(certPath: string, keyPath: string): void {
  const certDir = path.resolve(path.dirname(certPath), '..');

  const needsRefresh = (() => {
    if (!fs.existsSync(certPath) || !fs.existsSync(keyPath)) {
      return true;
    }

    try {
      const certificate = new X509Certificate(fs.readFileSync(certPath));
      return Date.parse(certificate.validTo) <= Date.now();
    } catch {
      return true;
    }
  })();

  if (!needsRefresh) {
    return;
  }

  execFileSync('bash', ['create.sh'], {
    cwd: certDir,
    stdio: 'inherit',
  });
}

describe('WebTransport Browser Transport', function() {
  this.timeout(60000);

  const chromiumExecutable = process.env.CHROMIUM_BIN || '/usr/bin/chromium';
  const browserBundlePath = path.resolve(process.cwd(), 'dist/browser/webtransport-test-runtime.js');
  const certificatePath = path.resolve(process.cwd(), '../../certs/out/localhost.crt');
  const certificateKeyPath = path.resolve(process.cwd(), '../../certs/out/localhost.key');
  const browserFixturePort = 24443;
  const serverPageUrl = 'https://localhost:22223/';
  const fixturePageUrl = `https://localhost:${browserFixturePort}/`;

  let browser: Browser | null = null;
  let fixtureServer: https.Server | null = null;

  before(async function() {
    if (!fs.existsSync(chromiumExecutable)) {
      this.skip();
    }

    ensureValidServerCertificate(certificatePath, certificateKeyPath);

    if (!fs.existsSync(browserBundlePath)) {
      throw new Error(`Browser test bundle not found: ${browserBundlePath}`);
    }

    if (!fs.existsSync(certificatePath)) {
      throw new Error(`Server certificate not found: ${certificatePath}`);
    }

    if (!fs.existsSync(certificateKeyPath)) {
      throw new Error(`Server certificate key not found: ${certificateKeyPath}`);
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
      fixtureServer!.listen(browserFixturePort, 'localhost', () => resolve());
    });

    const certificateSpki = getServerCertificateSpki(certificatePath);

    browser = await chromium.launch({
      executablePath: chromiumExecutable,
      headless: true,
      args: [
        '--enable-quic',
        // Note that Chrome/Chromium (the browser) does not allow custom CAs for QUIC,
        // so in addition to adding the root certificate to the certificate store,
        // you'll need to pass in --ignore-certificate-errors-spki-list=.. with the
        // certificate's SPKI to allow Chrome/Chromium to accept your custom certificate as valid.
        // https://www.chromium.org/quic/playing-with-quic/
        '--ignore-certificate-errors',
        `--ignore-certificate-errors-spki-list=${certificateSpki}`,
        '--origin-to-force-quic-on=localhost:22223',
      ],
    });
  });

  after(async function() {
    await browser?.close();
    await new Promise<void>((resolve) => fixtureServer?.close(() => resolve()));
  });

  async function createBrowserPage(pageUrl: string = fixturePageUrl) {
    if (!browser) {
      throw new Error('Browser is not available');
    }

    const context = await browser.newContext({
      ignoreHTTPSErrors: true,
    });
    const page = await context.newPage();
    await page.goto(pageUrl, { waitUntil: 'load' });
    await page.addScriptTag({ path: browserBundlePath });

    const certificateHash = getServerCertificateHash(certificatePath);
    const initResult = await page.evaluate(async ({ hashBytes }) => {
      return await (globalThis as any).nprpc_test_runtime.init(hashBytes);
    }, { hashBytes: certificateHash });

    return { context, page, initResult };
  }

  it('should establish a WebTransport session to the HTTP/3 test server in headless Chromium', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.openExplicitConnection();
      });

      expect((initResult as any).secureContext).to.equal(true);
      expect((result as any).endpointType).to.equal(EndPointType.WebTransport);
      expect((result as any).closedState).to.equal('open');
    } finally {
      await context.close();
    }
  });

  it('should resolve browser-visible objects through the nameserver over WebTransport', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.resolveMany([
          'nprpc_test_basic',
          'nprpc_test_server_control',
        ]);
      });

      expect((result as any).connectionCount).to.equal(1);
      expect((result as any).resolved).to.have.lengthOf(2);
      for (const resolved of (result as any).resolved) {
        expect(resolved.found).to.equal(true);
        expect(resolved.endpointType).to.equal(EndPointType.WebTransport);
      }
      expect((result as any).resolved[0].classId).to.equal('nprpc_test/nprpc.test.TestBasic');
      expect((result as any).resolved[1].classId).to.equal('nprpc_test/nprpc.test.ServerControl');
    } finally {
      await context.close();
    }
  });

  it('should invoke TestBasic methods over WebTransport after nameserver resolution', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.callTestBasic();
      });

      expect((result as any).booleanResult).to.equal(true);
      expect((result as any).u32Result).to.equal(42);
      expect((result as any).endpointType).to.equal(EndPointType.WebTransport);
    } finally {
      await context.close();
    }
  });

  it('should reuse existing WebTransport connections across repeated RPC calls', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.repeatedTestBasicCalls(3);
      });

      expect((result as any).values).to.deep.equal([42, 42, 42]);
      expect((result as any).before).to.equal(1);
      expect((result as any).after).to.equal(2);
    } finally {
      await context.close();
    }
  });

  it('should receive a byte stream over WebTransport', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.receiveByteStream();
      });

      expect((result as any).values).to.deep.equal([0, 1, 2, 3, 4]);
      expect((result as any).endpointType).to.equal(EndPointType.WebTransport);
    } finally {
      await context.close();
    }
  });

  it('should echo a bidi byte stream over WebTransport', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage();

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.echoBidiByteStream();
      });

      expect((result as any).values).to.deep.equal(
        (result as any).input.map((value: number) => value ^ (result as any).mask),
      );
      expect((result as any).endpointType).to.equal(EndPointType.WebTransport);
    } finally {
      await context.close();
    }
  });

  it('should keep HTTP RPC working from a cross-origin secure page', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage(fixturePageUrl);

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.callTestBasicHttp();
      });

      expect((result as any).booleanResult).to.equal(true);
      expect((result as any).u32Result).to.equal(42);
    } finally {
      await context.close();
    }
  });

  it('should keep repeated HTTP RPC calls working from a cross-origin secure page', async function() {
    if (!browser) {
      this.skip();
    }

    const { context, page, initResult } = await createBrowserPage(fixturePageUrl);

    try {
      if ((initResult as any).unsupported) {
        this.skip();
      }

      const result = await page.evaluate(async () => {
        return await (globalThis as any).nprpc_test_runtime.repeatedTestBasicHttpCalls(3);
      });

      expect((result as any).values).to.deep.equal([42, 42, 42]);
    } finally {
      await context.close();
    }
  });
});