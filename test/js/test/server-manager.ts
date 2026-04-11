import { spawn, spawnSync, ChildProcess, execSync } from 'child_process';
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

function ensureNprpcBpfCapabilities(binaryPath: string): void {
    const getcapResult = spawnSync('getcap', [binaryPath], { encoding: 'utf8' });
    if (getcapResult.error) {
        throw getcapResult.error;
    }

    const currentCaps = getcapResult.stdout ?? '';
    if (currentCaps.includes('cap_net_admin') && currentCaps.includes('cap_bpf')) {
        return;
    }

    // const sudoCheck = spawnSync('sudo', ['-v'], { stdio: 'inherit' });
    // if (sudoCheck.status !== 0) {
    //     throw new Error('Failed to obtain sudo permissions for setcap');
    // }

    const setcapResult = spawnSync(
        'sudo',
        ['setcap', 'cap_net_admin,cap_bpf+ep', binaryPath],
        { stdio: 'inherit' },
    );
    if (setcapResult.status !== 0) {
        throw new Error(`Failed to grant BPF capabilities to ${binaryPath}`);
    }
}

export class ServerManager {
    private serverProcess: ChildProcess | null = null;

    private static isProcessRunning(process: ChildProcess): boolean {
        return process.exitCode === null && process.signalCode === null;
    }

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
                ensureNprpcBpfCapabilities(path);
                this.serverProcess = spawn(path, [], {
                    stdio: ['pipe', outFile, errFile],
                    detached: false
                });

                if (this.serverProcess.pid) {
                    // Wait a bit for the server to initialize
                    await sleep(2000);
                    
                    // Check if process is still running
                    if (ServerManager.isProcessRunning(this.serverProcess)) {
                        console.log(`Test server started with PID: ${this.serverProcess.pid}`);
                        return true;
                    }
                    console.error(
                        `Test server process started but exited prematurely at path: ${path} ` +
                        `(exitCode=${this.serverProcess.exitCode}, signalCode=${this.serverProcess.signalCode})`
                    );
                    return false;
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
        if (this.serverProcess && ServerManager.isProcessRunning(this.serverProcess)) {
            return true;
        }

        // Start test server
        const testServerStarted = await this.startTestServer();
        if (!testServerStarted)
            return false;

        return true;
    }

    killServer() {
        if (this.serverProcess && ServerManager.isProcessRunning(this.serverProcess)) {
            console.log('Stopping test server...');
            this.serverProcess.kill('SIGTERM');
        }

        // Kill npnameserver as well if not already killed
        killProcessByName('npnameserver');

        this.serverProcess = null;
    }
}

const sharedServerManager = new ServerManager();

export function getSharedServerManager(): ServerManager {
    return sharedServerManager;
}
