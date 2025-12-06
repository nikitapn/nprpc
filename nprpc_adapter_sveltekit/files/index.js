// @nprpc/adapter-sveltekit - Main entry point
// This file starts the SvelteKit handler connected via shared memory to NPRPC C++ server

import { handler, start, stop } from './handler.js';

// Channel ID can be provided via environment or build-time config
const channelId = process.env.NPRPC_CHANNEL_ID || CHANNEL_ID;

if (!channelId) {
    console.error('Error: NPRPC_CHANNEL_ID environment variable or build-time channelId must be set');
    process.exit(1);
}

console.log(`Starting NPRPC SvelteKit handler with channel: ${channelId}`);

// Start listening for requests via shared memory
start(channelId);

// Handle graceful shutdown
process.on('SIGTERM', () => {
    console.log('Received SIGTERM, shutting down...');
    stop();
    process.exit(0);
});

process.on('SIGINT', () => {
    console.log('Received SIGINT, shutting down...');
    stop();
    process.exit(0);
});

console.log('NPRPC SvelteKit handler ready');
