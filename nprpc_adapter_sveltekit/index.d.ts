import type { Adapter } from '@sveltejs/kit';

export interface AdapterOptions {
    /**
     * Build output directory
     * @default 'build'
     */
    out?: string;

    /**
     * Shared memory channel ID for IPC with NPRPC server
     * If not provided, will be auto-generated at runtime
     */
    channelId?: string;

    /**
     * Whether to precompress static assets with gzip and brotli
     * @default true
     */
    precompress?: boolean;
}

declare function adapter(options?: AdapterOptions): Adapter;
export default adapter;
