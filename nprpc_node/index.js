// @nprpc/node_addon - JavaScript entry point
// Loads the native N-API addon

const path = require('path');

// Try to load the native addon
let addon;
const candidates = [
    './nprpc_shm.node',
    './build/Release/nprpc_shm.node',
    './build/Debug/nprpc_shm.node'
];

let lastError = null;
for (const candidate of candidates) {
    try {
        addon = require(candidate);
        break;
    } catch (error) {
        lastError = error;
    }
}

if (!addon) {
    throw new Error(
        `Failed to load nprpc_shm native addon. ` +
        `Make sure to run 'npm run build' first.\n` +
        `Searched: ${candidates.join(', ')}\n` +
        `Last error: ${lastError ? lastError.message : 'unknown error'}`
    );
}

// Export both for ESM and CommonJS compatibility
module.exports = addon;
module.exports.default = addon;
module.exports.ShmChannel = addon.ShmChannel;
