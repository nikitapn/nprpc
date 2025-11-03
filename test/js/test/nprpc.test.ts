// Setup WebSocket polyfill for Node.js BEFORE importing NPRPC
import './websocket-polyfill';

import { describe, it, before, after } from 'mocha';
import { expect } from 'chai';
// Use the Node.js compatible build
import * as NPRPC from 'nprpc/index.node.js';
import * as test from '../src/gen/nprpc_test';

// Test data constants (matching C++ tests)
const tstr1 = "test_string_1_very_long_test_string_1_very_long_test_string_1_very_long_test_string_1_very_long";
const tstr2 = "test_string_2_very_long_test_string_2_very_long_test_string_2_very_long_test_string_2_very_long";

// describe('NPRPC JavaScript Tests', function() {
//     this.timeout(30000); // 30 second timeout for all tests
    
//     let rpc: NPRPC.Rpc;
//     let nameserver: NPRPC.Nameserver;

//     before(async function() {
//         this.timeout(60000); // 60 seconds for setup
        
//         console.log('Connecting to running test server...');
        
//         // Get nameserver proxy directly (connects to existing server)
//         nameserver = NPRPC.get_nameserver('127.0.0.1');
        
//         console.log('✓ Connected to test server successfully');
//     });

//     after(async function() {
//         console.log('✓ Test completed');
//     });

//     async function createTestObject<T extends NPRPC.ObjectProxy>(
//         objectName: string,
//         proxyConstructor: new (oid: NPRPC.detail.ObjectId) => T
//     ): Promise<T> {
//         // Resolve object from nameserver
//         try {
//             const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
//             const found = await nameserver.Resolve(objectName, objRef);
            
//             if (!found) {
//                 throw new Error(`Failed to resolve object: ${objectName}`);
//             }

//             // Cast the resolved object to the desired type
//             return objRef.value as T;
//         } catch (error) {
//             throw new Error(`Failed to create test object for ${objectName}: ${error}`);
//         }
//     }

//     describe('Connection Tests', function() {
//         it('should establish RPC connection', async function() {
//             expect(rpc).to.not.be.undefined;
//             expect(nameserver).to.not.be.undefined;
//         });

//         it('should connect to nameserver', async function() {
//             // Try a simple nameserver operation
//             try {
//                 // This should work if nameserver is running
//                 const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
//                 const found = await nameserver.Resolve('nonexistent_object', objRef);
//                 expect(found).to.be.false;
//             } catch (error) {
//                 // Connection error means nameserver is not reachable
//                 throw new Error(`Nameserver connection failed: ${error}`);
//             }
//         });
//     });

//     describe('Basic Tests', function() {
//         it('should test basic connectivity', async function() {
//             // Basic connectivity test - just check that we can call nameserver
//             try {
//                 const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
//                 const found = await nameserver.Resolve('test_connectivity_check', objRef);
//                 // We expect this to return false since the object doesn't exist
//                 // But if this call succeeds, it means the connection works
//                 expect(found).to.be.false;
//                 console.log('Basic connectivity test passed');
//             } catch (error) {
//                 console.log(`Basic connectivity test failed: ${error}`);
//                 throw error;
//             }
//         });
//     });

//     describe('Test Object Resolution', function() {
//         it('should resolve test objects when server is running', async function() {
//             try {
//                 // Try to resolve a test object that should be created by C++ server
//                 const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
//                 const found = await nameserver.Resolve('nprpc_test_object', objRef);
                
//                 if (found) {
//                     console.log('Test object found - C++ server is running correctly');
//                     expect(objRef.value).to.not.be.undefined;
//                 } else {
//                     console.log('Test object not found - C++ server may not be running with test objects');
//                     (this as any).skip();
//                 }
//             } catch (error) {
//                 console.log(`Object resolution test failed: ${error}`);
//                 (this as any).skip();
//             }
//         });
//     });

//     describe('Future Implementation Tests', function() {
//         it('should implement TestBasic interface tests', function() {
//             // TODO: Implement when C++ server setup is complete
//             console.log('TestBasic implementation pending C++ server setup');
//             (this as any).skip();
//         });

//         it('should implement TestOptional interface tests', function() {
//             // TODO: Implement when C++ server setup is complete  
//             console.log('TestOptional implementation pending C++ server setup');
//             (this as any).skip();
//         });

//         it('should implement TestNested interface tests', function() {
//             // TODO: Implement when C++ server setup is complete
//             console.log('TestNested implementation pending C++ server setup');
//             (this as any).skip();
//         });

//         it('should implement large message tests', function() {
//             // TODO: Implement when C++ server setup is complete
//             console.log('Large message tests pending C++ server setup');
//             (this as any).skip();
//         });

//         it('should implement error handling tests', function() {
//             // TODO: Implement when C++ server setup is complete
//             console.log('Error handling tests pending C++ server setup');
//             (this as any).skip();
//         });
//     });
// });
