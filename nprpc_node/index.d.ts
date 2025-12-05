// TypeScript declarations for nprpc_node native addon

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
     * Try to receive data from the channel (non-blocking)
     * @returns Uint8Array with received data, or null if no data available
     */
    tryReceive(): Uint8Array | null;

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
