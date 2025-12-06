// @nprpc/adapter-sveltekit - Request handler
// Receives HTTP requests via shared memory, processes with SvelteKit, returns responses

import { Server } from 'SERVER';
import { manifest, prerendered, base } from 'MANIFEST';
import { ShmChannel } from 'nprpc_node';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = path.dirname(fileURLToPath(import.meta.url));
const server = new Server(manifest);

/** @type {ShmChannel | null} */
let channel = null;

/** @type {boolean} */
let running = false;

/** @type {NodeJS.Timeout | null} */
let pollTimer = null;

// Initialize the SvelteKit server
await server.init({
    env: /** @type {Record<string, string>} */ (process.env),
    read: (file) => {
        const filePath = path.join(dir, 'client', file);
        return fs.createReadStream(filePath);
    }
});

/**
 * Message format for HTTP request over shared memory:
 * {
 *   type: 'request',
 *   id: number,           // Request ID for matching response
 *   method: string,       // HTTP method
 *   url: string,          // Full URL
 *   headers: Record<string, string>,
 *   body?: string         // Base64 encoded body for POST/PUT
 * }
 * 
 * Response format:
 * {
 *   type: 'response',
 *   id: number,           // Matches request ID
 *   status: number,
 *   headers: Record<string, string>,
 *   body: string          // Base64 encoded body
 * }
 */

/**
 * Parse incoming request from shared memory
 * @param {Uint8Array} data
 * @returns {object | null}
 */
function parseRequest(data) {
    try {
        const text = new TextDecoder().decode(data);
        return JSON.parse(text);
    } catch (e) {
        console.error('Failed to parse request:', e);
        return null;
    }
}

/**
 * Serialize response for shared memory
 * @param {object} response
 * @returns {Uint8Array}
 */
function serializeResponse(response) {
    const text = JSON.stringify(response);
    return new TextEncoder().encode(text);
}

/**
 * Convert shared memory request to Web API Request
 * @param {object} req
 * @returns {Request}
 */
function toWebRequest(req) {
    const headers = new Headers(req.headers || {});
    
    /** @type {RequestInit} */
    const init = {
        method: req.method,
        headers
    };

    // Add body for methods that support it
    if (req.body && ['POST', 'PUT', 'PATCH', 'DELETE'].includes(req.method)) {
        init.body = Buffer.from(req.body, 'base64');
    }

    return new Request(req.url, init);
}

/**
 * Convert Web API Response to shared memory response
 * @param {number} requestId
 * @param {Response} response
 * @returns {Promise<object>}
 */
async function fromWebResponse(requestId, response) {
    const headers = /** @type {Record<string, string>} */ ({});
    response.headers.forEach((value, key) => {
        headers[key] = value;
    });

    const arrayBuffer = await response.arrayBuffer();
    const body = Buffer.from(arrayBuffer).toString('base64');

    return {
        type: 'response',
        id: requestId,
        status: response.status,
        headers,
        body
    };
}

/**
 * Handle a single request
 * @param {object} req
 */
async function handleRequest(req) {
    if (req.type !== 'request') {
        console.error('Unknown message type:', req.type);
        return;
    }

    try {
        // Check for prerendered page first
        const url = new URL(req.url);
        let pathname = url.pathname;
        
        // Normalize path
        if (pathname !== '/' && pathname.endsWith('/')) {
            pathname = pathname.slice(0, -1);
        }

        // Check if it's a prerendered page
        if (prerendered.has(pathname) || prerendered.has(pathname + '/')) {
            const prerenderedPath = path.join(dir, 'prerendered', base, pathname);
            
            // Try to serve prerendered HTML
            const htmlPath = prerenderedPath.endsWith('.html') 
                ? prerenderedPath 
                : `${prerenderedPath}/index.html`;
            
            if (fs.existsSync(htmlPath)) {
                const content = fs.readFileSync(htmlPath);
                const response = {
                    type: 'response',
                    id: req.id,
                    status: 200,
                    headers: {
                        'content-type': 'text/html; charset=utf-8',
                        'content-length': String(content.length)
                    },
                    body: content.toString('base64')
                };
                sendResponse(response);
                return;
            }
        }

        // Convert to Web API Request
        const webRequest = toWebRequest(req);

        // Let SvelteKit handle it
        const webResponse = await server.respond(webRequest, {
            getClientAddress: () => req.clientAddress || '127.0.0.1',
            platform: {
                channelId: channel?.getChannelId()
            }
        });

        // Convert response and send back
        const response = await fromWebResponse(req.id, webResponse);
        sendResponse(response);

    } catch (error) {
        console.error('Error handling request:', error);
        
        // Send error response
        const response = {
            type: 'response',
            id: req.id,
            status: 500,
            headers: {
                'content-type': 'text/plain'
            },
            body: Buffer.from('Internal Server Error').toString('base64')
        };
        sendResponse(response);
    }
}

/**
 * Send response back via shared memory
 * @param {object} response
 */
function sendResponse(response) {
    if (!channel || !channel.isOpen()) {
        console.error('Cannot send response: channel not open');
        return;
    }

    const data = serializeResponse(response);
    const success = channel.send(data);
    
    if (!success) {
        console.error('Failed to send response via shared memory');
    }
}

/**
 * Poll for incoming requests
 */
function pollRequests() {
    if (!channel || !channel.isOpen() || !running) {
        return;
    }

    while (channel.hasData()) {
        const data = channel.tryReceive();
        if (data) {
            const req = parseRequest(data);
            if (req) {
                // Handle request asynchronously
                handleRequest(req).catch(err => {
                    console.error('Unhandled error in request handler:', err);
                });
            }
        }
    }

    // Schedule next poll
    if (running) {
        pollTimer = setTimeout(pollRequests, 1);
    }
}

/**
 * Start the handler - connect to shared memory channel
 * @param {string} channelId
 */
export function start(channelId) {
    if (channel) {
        console.warn('Handler already started');
        return;
    }

    try {
        // Connect as client to the C++ server's channel
        channel = new ShmChannel(channelId, { isServer: false, create: false });
        
        if (!channel.isOpen()) {
            throw new Error(`Failed to open channel: ${channel.getError()}`);
        }

        running = true;
        console.log(`Connected to shared memory channel: ${channelId}`);

        // Start polling for requests
        pollRequests();

    } catch (error) {
        console.error('Failed to start handler:', error);
        throw error;
    }
}

/**
 * Stop the handler - disconnect from shared memory
 */
export function stop() {
    running = false;
    
    if (pollTimer) {
        clearTimeout(pollTimer);
        pollTimer = null;
    }

    if (channel) {
        channel.close();
        channel = null;
    }

    console.log('Handler stopped');
}

/**
 * Export handler for custom server integration
 * @param {object} req - Request object from shared memory
 * @returns {Promise<object>} Response object for shared memory
 */
export async function handler(req) {
    const webRequest = toWebRequest(req);
    
    const webResponse = await server.respond(webRequest, {
        getClientAddress: () => req.clientAddress || '127.0.0.1',
        platform: {}
    });

    return fromWebResponse(req.id || 0, webResponse);
}
