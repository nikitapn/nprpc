import { spawn, ChildProcess, execSync } from 'child_process';
import { promisify } from 'util';
import fs from 'fs';

const sleep = promisify(setTimeout);

/**
 * Kill a process by name using pgrep/pkill
 */
function killProcessByName(processName: string): void {
    try {
        // Use pgrep to find PIDs by name, then kill them
        execSync(`pkill -SIGTERM -f "${processName}"`, { stdio: 'pipe' });
        console.log(`Killed process: ${processName}`);
    } catch (error) {
        // No process found or already terminated - this is fine
        console.log(`No process found for: ${processName}`);
    }
}

export class ServerManager {
    private serverProcess: ChildProcess | null = null;

    async startTestServer(): Promise<boolean> {
        console.log('Starting test server...');
        
        // Try different paths for the test server binary
        const serverPaths = [
            '/home/nikita/projects/nprpc/.build_relwith_debinfo/test/nprpc_server_test',
            '/home/nikita/projects/nprpc/.build_release/test/nprpc_server_test',
            '/home/nikita/projects/nprpc/.build_debug/test/nprpc_server_test'
        ];

        const outFile = fs.openSync('/tmp/nprpc_test_server_stdout.log', 'w');
        const errFile = fs.openSync('/tmp/nprpc_test_server_stderr.log', 'w');

        for (const path of serverPaths) {
            if (!fs.existsSync(path))
                continue;
            try {
                this.serverProcess = spawn(path, [], {
                    stdio: ['pipe', outFile, errFile],
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

    async startAll(): Promise<boolean> {
        console.log('Starting test server...');
        // Start test server
        const testServerStarted = await this.startTestServer();
        if (!testServerStarted)
            return false;

        console.log('Test server started successfully');
        return true;
    }

    killServer() {
        if (this.serverProcess && !this.serverProcess.killed) {
            console.log('Stopping test server...');
            this.serverProcess.kill('SIGTERM');
        }

        // Kill npnameserver as well if not already killed
        killProcessByName('npnameserver');

        this.serverProcess = null;
    }
}
