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

/**
 * Start the handler - connect to shared memory channel
 * @param channelId - The shared memory channel ID to connect to
 */
export function start(channelId: string): void;

/**
 * Stop the handler - disconnect from shared memory
 */
export function stop(): void;

/**
 * Handle a single request (for custom server integration)
 * @param req - Request from shared memory
 * @returns Response for shared memory
 */
export function handler(req: ShmRequest): Promise<ShmResponse>;
