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
