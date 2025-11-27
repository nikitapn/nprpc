import { spawn, ChildProcess } from 'child_process';
import { promisify } from 'util';
import fs from 'fs';

const sleep = promisify(setTimeout);

export class ServerManager {
    private serverProcess: ChildProcess | null = null;
    private nameserverProcess: ChildProcess | null = null;

    async startNameserver(): Promise<boolean> {
        console.log('Starting nameserver...');
        
        // Try different paths for the nameserver binary
        const nameserverPaths = [
            '/home/nikita/projects/nprpc/.build_release/npnameserver',
            '/home/nikita/projects/nprpc/.build_debug/npnameserver'
        ];

        for (const path of nameserverPaths) {
            if (!fs.existsSync(path))
                continue;
            try {
                this.nameserverProcess = spawn(path, [], {
                    stdio: ['ignore', 'pipe', 'pipe'],
                    detached: false
                });

                if (this.nameserverProcess.pid) {
                    console.log(`Nameserver started with PID: ${this.nameserverProcess.pid}`);
                    
                    // Wait a bit for the nameserver to initialize
                    await sleep(1000);
                    
                    // Check if process is still running
                    if (!this.nameserverProcess.killed) {
                        return true;
                    }
                }
            } catch (error) {
                console.log(`Failed to start nameserver at ${path}: ${error}`);
                continue;
            }
        }

        console.error('Failed to start nameserver from any of the tried paths');
        return false;
    }

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

    stopNameserver(): void {
        if (this.nameserverProcess && !this.nameserverProcess.killed) {
            console.log('Stopping nameserver...');
            this.nameserverProcess.kill('SIGTERM');
            this.nameserverProcess = null;
        }
    }

    stopTestServer(): void {
        if (this.serverProcess && !this.serverProcess.killed) {
            console.log('Stopping test server...');
            this.serverProcess.kill('SIGTERM');
            this.serverProcess = null;
        }
    }

    async startAll(): Promise<boolean> {
        console.log('Starting nameserver and test server...');
        
        // Start nameserver first
        const nameserverStarted = await this.startNameserver();
        if (!nameserverStarted) {
            return false;
        }

        // Start test server
        const testServerStarted = await this.startTestServer();
        if (!testServerStarted) {
            this.stopNameserver();
            return false;
        }

        console.log('All servers started successfully');
        return true;
    }

    stopAll(): void {
        this.stopTestServer();
        this.stopNameserver();
    }
}
