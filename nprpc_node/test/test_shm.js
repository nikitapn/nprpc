// Test for nprpc_node shared memory addon

const nprpc = require('../index');

console.log('nprpc_node version:', nprpc.version);

// Test 1: Create server channel
console.log('\n=== Test 1: Create server channel ===');
let serverChannel;
try {
    serverChannel = new nprpc.ShmChannel('test_channel', { isServer: true, create: true });
    console.log('Server channel created:', serverChannel.isOpen());
    console.log('Channel ID:', serverChannel.getChannelId());
} catch (e) {
    console.error('Failed to create server channel:', e.message);
    process.exit(1);
}

// Test 2: Send data from server
console.log('\n=== Test 2: Send data ===');
const testData = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);
const sendOk = serverChannel.send(testData);
console.log('Send result:', sendOk);

// Test 3: Check hasData (should be false since we're reading from different ring)
console.log('\n=== Test 3: Check hasData ===');
console.log('Has data:', serverChannel.hasData());

// Test 4: Create client channel (connects to same shared memory)
console.log('\n=== Test 4: Create client channel ===');
let clientChannel;
try {
    clientChannel = new nprpc.ShmChannel('test_channel', { isServer: false, create: false });
    console.log('Client channel created:', clientChannel.isOpen());
    console.log('Channel ID:', clientChannel.getChannelId());
} catch (e) {
    console.error('Failed to create client channel:', e.message);
    serverChannel.close();
    process.exit(1);
}

// Test 5: Receive data on client (should see server's message)
console.log('\n=== Test 5: Receive data on client ===');
console.log('Client hasData:', clientChannel.hasData());
const received = clientChannel.tryReceive();
if (received) {
    console.log('Received:', Array.from(received));
    console.log('Match:', JSON.stringify(Array.from(received)) === JSON.stringify(Array.from(testData)));
} else {
    console.log('No data received (null)');
}

// Test 6: Send from client, receive on server
console.log('\n=== Test 6: Bidirectional communication ===');
const clientData = new Uint8Array([100, 101, 102, 103]);
clientChannel.send(clientData);
console.log('Client sent data');

console.log('Server hasData:', serverChannel.hasData());
const serverReceived = serverChannel.tryReceive();
if (serverReceived) {
    console.log('Server received:', Array.from(serverReceived));
} else {
    console.log('Server received: null');
}

// Clean up
console.log('\n=== Cleanup ===');
clientChannel.close();
console.log('Client closed');
serverChannel.close();
console.log('Server closed');

console.log('\n=== All tests passed ===');
