// nprpc_node - JavaScript entry point
// Loads the native N-API addon

const path = require('path');

// Try to load the native addon
let addon;
try {
    // First try the standard node-gyp build location
    addon = require('./build/Release/nprpc_shm.node');
} catch (e1) {
    try {
        // Try debug build
        addon = require('./build/Debug/nprpc_shm.node');
    } catch (e2) {
        throw new Error(
            `Failed to load nprpc_shm native addon. ` +
            `Make sure to run 'npm run build' first.\n` +
            `Release error: ${e1.message}\n` +
            `Debug error: ${e2.message}`
        );
    }
}

// Export both for ESM and CommonJS compatibility
module.exports = addon;
module.exports.default = addon;
module.exports.ShmChannel = addon.ShmChannel;
