// Test large messages with nprpc_node shared memory addon

const nprpc = require('../index');

console.log('Testing large messages...\n');

// Test sizes: 1KB, 64KB, 1MB, 8MB
const testSizes = [
    1024,           // 1 KB
    64 * 1024,      // 64 KB
    1024 * 1024,    // 1 MB
    8 * 1024 * 1024 // 8 MB (well within ring buffer)
];

function createTestData(size) {
    const data = new Uint8Array(size);
    for (let i = 0; i < size; i++) {
        data[i] = i % 256;
    }
    return data;
}

function verifyData(original, received) {
    if (!received) return false;
    if (original.length !== received.length) return false;
    for (let i = 0; i < original.length; i++) {
        if (original[i] !== received[i]) return false;
    }
    return true;
}

// Create channels
const serverChannel = new nprpc.ShmChannel('large_test', { isServer: true, create: true });
const clientChannel = new nprpc.ShmChannel('large_test', { isServer: false, create: false });

console.log('Channels created');

let allPassed = true;

for (const size of testSizes) {
    const sizeName = size >= 1024 * 1024 
        ? `${size / (1024 * 1024)} MB` 
        : size >= 1024 
            ? `${size / 1024} KB`
            : `${size} bytes`;
    
    console.log(`\nTesting ${sizeName}...`);
    
    const testData = createTestData(size);
    const startSend = Date.now();
    const sendOk = serverChannel.send(testData);
    const sendTime = Date.now() - startSend;
    
    if (!sendOk) {
        console.log(`  FAILED: Send returned false`);
        allPassed = false;
        continue;
    }
    
    const startRecv = Date.now();
    const received = clientChannel.tryReceive();
    const recvTime = Date.now() - startRecv;
    
    const verified = verifyData(testData, received);
    
    if (verified) {
        const throughput = (size / (1024 * 1024)) / ((sendTime + recvTime) / 1000);
        console.log(`  PASSED: Send=${sendTime}ms, Recv=${recvTime}ms`);
        if (sendTime + recvTime > 0) {
            console.log(`  Throughput: ~${throughput.toFixed(2)} MB/s`);
        }
    } else {
        console.log(`  FAILED: Data verification failed`);
        if (received) {
            console.log(`  Expected length: ${testData.length}, Got: ${received.length}`);
        } else {
            console.log(`  Received null`);
        }
        allPassed = false;
    }
}

// Clean up
clientChannel.close();
serverChannel.close();

console.log('\n=== Large message tests:', allPassed ? 'ALL PASSED' : 'SOME FAILED', '===');
process.exit(allPassed ? 0 : 1);
