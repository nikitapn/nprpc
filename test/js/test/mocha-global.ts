import { getSharedServerManager } from './server-manager';

export async function mochaGlobalSetup() {
  const started = await getSharedServerManager().startAll();
  if (!started) {
    throw new Error('Failed to start NPRPC integration server');
  }
}

export async function mochaGlobalTeardown() {
  getSharedServerManager().killServer();
}