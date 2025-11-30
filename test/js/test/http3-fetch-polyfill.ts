// HTTP/3 fetch polyfill for Node.js using curl
// This allows testing HTTP/3 RPC calls in Node.js environment

import { spawn } from 'child_process';

interface Http3FetchOptions {
  method?: string;
  headers?: Record<string, string>;
  body?: ArrayBuffer | Uint8Array | string;
}

/**
 * HTTP/3 fetch implementation using curl --http3-only
 * 
 * This is a drop-in replacement for fetch() that uses HTTP/3 via curl.
 * Requires curl with HTTP/3 support (curl 7.88+ with nghttp3).
 */
export async function http3Fetch(url: string, options: Http3FetchOptions = {}): Promise<Response> {
  return new Promise((resolve, reject) => {
    const method = options.method || 'GET';
    
    // Build curl arguments
    const args = [
      '--http3-only',      // Force HTTP/3 only
      '--insecure',        // Accept self-signed certs (for testing)
      '-s',                // Silent mode
      '-i',                // Include headers in output
      '-X', method,
    ];
    
    // Add custom headers
    if (options.headers) {
      for (const [key, value] of Object.entries(options.headers)) {
        args.push('-H', `${key}: ${value}`);
      }
    }
    
    // For POST with body, use stdin
    if (options.body) {
      args.push('--data-binary', '@-');
    }
    
    args.push(url);
    
    const curl = spawn('curl', args);
    
    const chunks: Buffer[] = [];
    let stderrData = '';
    
    curl.stdout.on('data', (data: Buffer) => {
      chunks.push(data);
    });
    
    curl.stderr.on('data', (data: Buffer) => {
      stderrData += data.toString();
    });
    
    curl.on('close', (code) => {
      if (code !== 0) {
        reject(new Error(`curl exited with code ${code}: ${stderrData}`));
        return;
      }
      
      const fullResponse = Buffer.concat(chunks);
      const responseStr = fullResponse.toString('utf-8');
      
      // Parse HTTP response (headers + body)
      // Find the blank line separating headers from body
      const headerEndIndex = responseStr.indexOf('\r\n\r\n');
      if (headerEndIndex === -1) {
        reject(new Error('Invalid HTTP response from curl'));
        return;
      }
      
      const headerSection = responseStr.substring(0, headerEndIndex);
      const bodyBuffer = fullResponse.slice(headerEndIndex + 4);
      
      // Parse status line and headers
      const headerLines = headerSection.split('\r\n');
      const statusLine = headerLines[0];
      
      // Parse status: "HTTP/3 200" or "HTTP/3 200 OK"
      const statusMatch = statusLine.match(/HTTP\/[\d.]+ (\d+)/);
      if (!statusMatch) {
        reject(new Error(`Failed to parse status line: ${statusLine}`));
        return;
      }
      
      const status = parseInt(statusMatch[1], 10);
      const statusText = statusLine.substring(statusLine.indexOf(' ', statusLine.indexOf(' ') + 1) + 1) || '';
      
      // Parse headers
      const headers = new Headers();
      for (let i = 1; i < headerLines.length; i++) {
        const colonIndex = headerLines[i].indexOf(':');
        if (colonIndex > 0) {
          const key = headerLines[i].substring(0, colonIndex).trim();
          const value = headerLines[i].substring(colonIndex + 1).trim();
          headers.append(key, value);
        }
      }
      
      // Create Response object
      const response = new Response(bodyBuffer, {
        status,
        statusText,
        headers,
      });
      
      resolve(response);
    });
    
    curl.on('error', (err) => {
      reject(new Error(`Failed to spawn curl: ${err.message}`));
    });
    
    // Send body if present
    if (options.body) {
      let bodyData: Buffer;
      if (options.body instanceof ArrayBuffer) {
        bodyData = Buffer.from(options.body);
      } else if (options.body instanceof Uint8Array) {
        bodyData = Buffer.from(options.body);
      } else {
        bodyData = Buffer.from(options.body, 'utf-8');
      }
      curl.stdin.write(bodyData);
      curl.stdin.end();
    } else {
      curl.stdin.end();
    }
  });
}

/**
 * Check if curl has HTTP/3 support
 */
export async function checkHttp3Support(): Promise<boolean> {
  return new Promise((resolve) => {
    const curl = spawn('curl', ['--version']);
    let output = '';
    
    curl.stdout.on('data', (data: Buffer) => {
      output += data.toString();
    });
    
    curl.on('close', () => {
      resolve(output.includes('HTTP3'));
    });
    
    curl.on('error', () => {
      resolve(false);
    });
  });
}

/**
 * Install HTTP/3 fetch as global fetch replacement
 * 
 * Usage:
 *   import { installHttp3Fetch } from './http3-fetch-polyfill';
 *   await installHttp3Fetch();
 *   
 *   // Now fetch() uses HTTP/3 via curl (on same port as HTTP)
 *   const response = await fetch('https://localhost:22223/rpc', { ... });
 */
export async function installHttp3Fetch(): Promise<boolean> {
  const hasHttp3 = await checkHttp3Support();
  
  if (!hasHttp3) {
    console.warn('[HTTP/3] curl does not have HTTP/3 support, skipping polyfill');
    return false;
  }
  
  console.log('[HTTP/3] Installing HTTP/3 fetch polyfill (using curl --http3-only)');
  
  // Save original fetch
  const originalFetch = globalThis.fetch;
  
  // Install wrapper that uses HTTP/3 for https:// URLs on the test port
  // HTTP/3 now uses same port as HTTP/1.1 (22223)
  (globalThis as any).fetch = async (input: RequestInfo | URL, init?: RequestInit): Promise<Response> => {
    const url = typeof input === 'string' ? input : input.toString();
    
    // Use HTTP/3 for localhost HTTPS on port 22223 (same as HTTP)
    if (url.startsWith('https://localhost:22223') || url.startsWith('https://127.0.0.1:22223')) {
      return http3Fetch(url, {
        method: init?.method,
        headers: init?.headers as Record<string, string>,
        body: init?.body as ArrayBuffer | Uint8Array | string,
      });
    }
    
    // Fall back to original fetch for everything else
    return originalFetch(input, init);
  };
  
  return true;
}

export default http3Fetch;
