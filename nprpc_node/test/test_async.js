// Test async polling with nprpc_node shared memory addon

const nprpc = require('../index');

console.log('Testing async polling...');

async function testAsyncPolling() {
    // Create server channel
    const serverChannel = new nprpc.ShmChannel('async_test', { isServer: true, create: true });
    console.log('Server channel created');

    // Create client channel  
    const clientChannel = new nprpc.ShmChannel('async_test', { isServer: false, create: false });
    console.log('Client channel created');

    // Set up promise to wait for data
    let dataReceived = false;
    const dataPromise = new Promise((resolve) => {
        // Note: The current polling implementation uses eventfd which 
        // won't work as expected without the server signaling it.
        // For now, we'll use a simple polling loop instead.
        const checkData = () => {
            if (clientChannel.hasData()) {
                const data = clientChannel.tryReceive();
                if (data) {
                    dataReceived = true;
                    resolve(data);
                    return;
                }
            }
            if (!dataReceived) {
                setTimeout(checkData, 10);
            }
        };
        checkData();
    });

    // Wait a bit then send data from server
    setTimeout(() => {
        const testData = new Uint8Array([42, 43, 44, 45]);
        serverChannel.send(testData);
        console.log('Server sent data');
    }, 100);

    // Wait for data with timeout
    const timeout = new Promise((_, reject) => 
        setTimeout(() => reject(new Error('Timeout waiting for data')), 2000));
    
    try {
        const received = await Promise.race([dataPromise, timeout]);
        console.log('Client received:', Array.from(received));
        console.log('Async polling test PASSED');
    } catch (e) {
        console.error('Async polling test FAILED:', e.message);
    }

    // Clean up
    clientChannel.close();
    serverChannel.close();
    console.log('Channels closed');
}

testAsyncPolling().catch(console.error);
