// Setup WebSocket polyfill for Node.js BEFORE importing NPRPC
import './websocket-polyfill';

import { describe, it } from 'mocha';
import { expect } from 'chai';
// Use the Node.js compatible build
const NPRPC = require('nprpc/index.node.js');

describe('NPRPC WebSocket Polyfill Test', function() {
    this.timeout(10000);
    
    it('should have WebSocket available globally', function() {
        expect(typeof (global as any).WebSocket).to.equal('function');
    });

    it('should be able to create NPRPC Connection instance', function() {
        try {
            const endpoint = new NPRPC.EndPoint(
                NPRPC.EndPointType.WebSocket,
                'localhost',
                15001
            );
            expect(endpoint).to.not.be.undefined;
            expect(endpoint.hostname).to.equal('localhost');
            expect(endpoint.port).to.equal(15001);
        } catch (error) {
            throw new Error(`Failed to create NPRPC EndPoint: ${error}`);
        }
    });

    it('should be able to create Connection without immediate connection', function() {
        try {
            const endpoint = new NPRPC.EndPoint(
                NPRPC.EndPointType.WebSocket,
                'localhost',
                15001
            );
            
            // This will create a WebSocket connection but might fail to connect
            // since no server is running - that's expected for this test
            const connection = new NPRPC.Connection(endpoint);
            expect(connection).to.not.be.undefined;

            // Clean up - close the connection
            setTimeout(() => {
                try {
                    (connection as any).ws?.close();
                } catch (e) {
                    // Ignore cleanup errors
                }
            }, 100);
        } catch (error) {
            throw new Error(`Failed to create NPRPC Connection: ${error}`);
        }
    });

    it('should be able to get nameserver proxy', function() {
        try {
            const nameserver = NPRPC.get_nameserver('127.0.0.1');
            expect(nameserver).to.not.be.undefined;
            expect(nameserver.data.urls).to.include('ws://127.0.0.1:15001');
        } catch (error) {
            throw new Error(`Failed to create nameserver proxy: ${error}`);
        }
    });
});
