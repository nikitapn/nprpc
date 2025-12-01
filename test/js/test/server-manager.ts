import { spawn, ChildProcess } from 'child_process';
import { promisify } from 'util';
import fs from 'fs';

const sleep = promisify(setTimeout);

export class ServerManager {
    private serverProcess: ChildProcess | null = null;
    private nameserverProcess: ChildProcess | null = null;

    async startTestServer(): Promise<boolean> {
        console.log('Starting test server...');
        
        // Try different paths for the test server binary
        const serverPaths = [
            '/home/nikita/projects/nprpc/.build_release/test/nprpc_server_test',
            '/home/nikita/projects/nprpc/.build_debug/test/nprpc_server_test'
        ];

        for (const path of serverPaths) {
            if (!fs.existsSync(path))
                continue;
            try {
                this.serverProcess = spawn(path, [], {
                    stdio: ['pipe', 'pipe', 'pipe'],
                    detached: false
                });

                if (this.serverProcess.pid) {
                    console.log(`Test server started with PID: ${this.serverProcess.pid}`);
                    
                    // Wait a bit for the server to initialize
                    await sleep(2000);
                    
                    // Check if process is still running
                    if (!this.serverProcess.killed) {
                        return true;
                    }
                }
            } catch (error) {
                console.log(`Failed to start test server at ${path}: ${error}`);
                continue;
            }
        }

        console.error('Failed to start test server from any of the tried paths');
        return false;
    }

    stopTestServer(): void {
        if (this.serverProcess && !this.serverProcess.killed) {
            console.log('Stopping test server...');
            this.serverProcess.kill('SIGTERM');
            this.serverProcess = null;
        }
    }

    async startAll(): Promise<boolean> {
        console.log('Starting test server...');

        // Start test server
        const testServerStarted = await this.startTestServer();
        if (!testServerStarted)
            return false;

        console.log('Test server started successfully');
        return true;
    }

    stopAll(): void {
        this.stopTestServer();
    }
}
