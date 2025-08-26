// Setup WebSocket polyfill for Node.js BEFORE importing NPRPC
import './websocket-polyfill';

import { describe, it, before, after } from 'mocha';
import { expect } from 'chai';
// Use the Node.js compatible build
// const NPRPC = require('nprpc/index.node.js');
import * as NPRPC from 'nprpc';
import * as test from '../src/gen/test';
import { ServerManager } from './server-manager';

// Test data constants (matching C++ tests)
const tstr1 = "test_string_1_very_long_test_string_1_very_long_test_string_1_very_long_test_string_1_very_long";
const tstr2 = "test_string_2_very_long_test_string_2_very_long_test_string_2_very_long_test_string_2_very_long";

describe('NPRPC Integration Tests', function() {
    this.timeout(30000); // 30 second timeout for all tests
    
    const serverManager = new ServerManager();
    let rpc: NPRPC.Rpc;
    let nameserver: NPRPC.Nameserver;

    before(async function() {
        this.timeout(60000); // 60 seconds for setup
        
        console.log('Setting up NPRPC test environment...');
        
        // Start nameserver and test server
        const serversStarted = await serverManager.startAll();
        if (!serversStarted) {
            throw new Error('Failed to start servers');
        }

        // Initialize RPC connection
        try {
            rpc = await NPRPC.init(false);
            console.log('RPC initialized successfully');
        } catch (error) {
            serverManager.stopAll();
            throw new Error(`Failed to initialize RPC: ${error}`);
        }

        // Get nameserver proxy
        try {
            nameserver = NPRPC.get_nameserver('127.0.0.1');
            console.log('Nameserver proxy created');
            
            // Wait a bit more for test server to register objects
            await new Promise(resolve => setTimeout(resolve, 3000));
        } catch (error) {
            serverManager.stopAll();
            throw new Error(`Failed to get nameserver: ${error}`);
        }

        console.log('NPRPC test environment setup complete');
    });

    after(function() {
        console.log('Cleaning up NPRPC test environment...');
        serverManager.stopAll();
    });

    async function resolveTestObject<T extends NPRPC.ObjectProxy>(objectName: string, obj: new() => T): Promise<any> {
        try {
            const objRef = NPRPC.make_ref<NPRPC.ObjectProxy>();
            const found = await nameserver.Resolve(objectName, objRef);
            
            if (!found) {
                throw new Error(`Failed to resolve object: ${objectName}`);
            }

            const narrowedObj = NPRPC.narrow(objRef.value, obj);
            if (!narrowedObj) {
                throw new Error(`Failed to narrow object to desired type: ${objectName}`);
            }

            return narrowedObj;
        } catch (error) {
            throw new Error(`Failed to resolve test object ${objectName}: ${error}`);
        }
    }

    describe('TestBasic Interface', function() {
        let testBasic: test.TestBasic;

        before(async function() {
            testBasic = await resolveTestObject('nprpc_test_basic', test.TestBasic);
        });

        it('should return boolean true', async function() {
            const result = await testBasic.ReturnBoolean();
            expect(result).to.be.true;
        });

        it('should handle input parameters correctly', async function() {
            // Create test data matching C++ test
            const testData = new Array(256);
            for (let i = 0; i < 256; i++) {
                testData[i] = i;
            }

            const result = await testBasic.In(100, true, testData);
            expect(result).to.be.true;
        });

        it('should handle output parameters correctly', async function() {
            // Create ref objects for output parameters
            const aRef = NPRPC.make_ref();
            const bRef = NPRPC.make_ref();
            const cArray = NPRPC.make_ref<number[]>();
            
            // Call the Out method
            await testBasic.Out(aRef, bRef, cArray);
            
            // Verify output values match C++ implementation
            expect(aRef.value).to.equal(100);
            expect(bRef.value).to.be.true;
            expect(cArray.value.length).to.equal(256);
            
            // Verify the array content (should be 0, 1, 2, ..., 255)
            for (let i = 0; i < 256; i++) {
                expect(cArray.value[i]).to.equal(i);
            }
        });
    });

    describe('TestOptional Interface', function() {
        let testOptional: test.TestOptional;

        before(async function() {
            testOptional = await resolveTestObject('nprpc_test_optional', test.TestOptional);
        });

        it('should handle empty optional values', async function() {
            const result = await testOptional.InEmpty(); // No parameter = empty optional
            expect(result).to.be.true;
        });

        it('should handle optional values with data', async function() {
            // Create test AAA object
            const testAAA = {
                a: 100,
                b: "test_b",
                c: "test_c"
            };
            const result = await testOptional.In(100, testAAA);
            expect(result).to.be.true;
        });

        it('should return empty optional values', async function() {
            // OutEmpty and Out methods don't return values, they are void methods
            // The return data would be through optional output parameters if they existed
            try {
                const a = NPRPC.make_ref<number>();
                await testOptional.OutEmpty(a);
                expect(a.value).to.be.undefined; // Should remain undefined for empty optional
            } catch (error) {
                console.error('OutEmpty call failed:', error);
                throw error;
            }
        });

        it('should return optional values with data', async function() {
            try {
                const a = NPRPC.make_ref<number>();
                await testOptional.Out(a);
                expect(a.value).to.equal(100); // Should be set by server
            } catch (error) {
                console.error('Out call failed:', error);
                throw error;
            }
        });
        
        it('should return optional', async function() {
            try {
                const opt = await testOptional.ReturnOpt1();
                expect(opt.str).to.equal('test_string');
                expect(opt.stream).to.be.an('array').that.has.lengthOf(10);
                expect(opt.stream).to.deep.equal([0,1,2,3,4,5,6,7,8,9]);
            } catch (error) {
                console.error('Out call failed:', error);
                throw error;
            }
        });
    });

    describe('TestNested Interface', function() {
        let testNested: test.TestNested;
        before(async function() {
            testNested = await resolveTestObject('nprpc_test_nested', test.TestNested);
        });

        it('should handle nested structures correctly', async function() {
            try {
                // let reference = NPRPC.make_ref<test.BBB>();
                // TODO: fix npidl generation for nested structs
                // Need to add empty struct constuction `a.value = {}`
                //   let vv = opt.value.a_d(), index_2 = 0;
                //   (a.value.a as Array<any>) = new Array<any>(vv.elements_size)
                //   for (let e of vv) {
                //     a.value.a[index_2].a = e.a;
                //     a.value.a[index_2].b = e.b;
                //     a.value.a[index_2].c = e.c;
                //     ++index_2;
                //   }
                // const result = await testNested.Out(reference);
            } catch (error) {
                console.error('Out call failed:', error);
                throw error;
            }
        });
    });
});
