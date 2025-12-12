// TypeScript declarations for nprpc_node native addon

/**
 * HTTP Request received via shared memory from C++ server
 */
export interface ShmRequest {
    type: 'request';
    /** Unique request ID for matching responses */
    id: number;
    /** HTTP method (GET, POST, etc.) */
    method: string;
    /** Full URL including query string */
    url: string;
    /** HTTP headers */
    headers: Record<string, string>;
    /** Base64 encoded request body (for POST/PUT) */
    body?: string;
    /** Client IP address */
    clientAddress?: string;
}

/**
 * HTTP Response to send back via shared memory
 */
export interface ShmResponse {
    type: 'response';
    /** Request ID this response is for */
    id: number;
    /** HTTP status code */
    status: number;
    /** HTTP response headers */
    headers: Record<string, string>;
    /** Base64 encoded response body */
    body: string;
}

export interface ShmChannelOptions {
    /** Whether this is the server side (creates shared memory) */
    isServer: boolean;
    /** Whether to create new ring buffers (true for server, false for client) */
    create: boolean;
}

/**
 * Native shared memory channel for zero-copy IPC
 */
export class ShmChannel {
    /**
     * Create a new shared memory channel
     * @param channelId Unique identifier for the channel (shared between client/server)
     * @param options Channel creation options
     */
    constructor(channelId: string, options: ShmChannelOptions);

    /**
     * Check if the channel is open and ready
     */
    isOpen(): boolean;

    /**
     * Get the channel identifier
     */
    getChannelId(): string | null;

    /**
     * Get the last error message if the channel failed to open
     */
    getError(): string;

    /**
     * Send data through the channel
     * @param data Data to send (Uint8Array)
     * @returns true if sent successfully, false if buffer is full
     */
    send(data: Uint8Array): boolean;

    /**
     * Send ShmResponse through the channel
     * @param obj Object to send
     * @returns true if sent successfully, false if buffer is full
     */
    sendSSRResponse(obj: ShmResponse): boolean;

    /**
     * Try to receive data from the channel (non-blocking)
     * @returns Uint8Array with received data, or null if no data available
     */
    tryReceive(): Uint8Array | null;

    /**
     * Try to receive data from the server-side channel (non-blocking)
     * @returns ShmRequest object, or null if no data available
     */
    tryReceiveSSRRequest(): ShmRequest | null;

    /**
     * Check if there is data available to read
     */
    hasData(): boolean;

    /**
     * Close the channel and release resources
     */
    close(): void;

    /**
     * Start polling for incoming data (async)
     * @param callback Function called when data is available (call tryReceive() in callback)
     * @returns true if polling started successfully
     */
    startPolling(callback: () => void): boolean;

    /**
     * Stop polling for incoming data
     */
    stopPolling(): void;
}

/**
 * Module version
 */
export const version: string;
