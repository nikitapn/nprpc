import * as fs from 'fs';
import * as path from 'path';
import { workspace, ExtensionContext, commands, window } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let currentPath: string | undefined;
let startGeneration = 0;

function isExecutable(filePath: string): boolean {
	try {
		fs.accessSync(filePath, fs.constants.X_OK);
		return fs.statSync(filePath).isFile();
	} catch {
		return false;
	}
}

function getNpidlPath(): string {
	return workspace.getConfiguration('npidl').get<string>('lsp.path')?.trim() || 'npidl';
}

function createClient(npidlPath: string): LanguageClient {
	// Do not set transport: TransportKind.stdio — the client would append
	// `--stdio`, which Boost.ProgramOptions rejects. Native executables
	// already use stdin/stdout when transport is left unset.
	const serverOptions: ServerOptions = {
		command: npidlPath,
		args: ['--lsp']
	};

	const clientOptions: LanguageClientOptions = {
		documentSelector: [
			{ scheme: 'file', language: 'npidl' }
		],
		synchronize: {
			fileEvents: workspace.createFileSystemWatcher('**/*.npidl')
		}
	};

	return new LanguageClient(
		'npidlLanguageServer',
		'NPIDL Language Server',
		serverOptions,
		clientOptions
	);
}

async function startClient(npidlPath: string): Promise<void> {
	if (path.isAbsolute(npidlPath) && !isExecutable(npidlPath)) {
		window.showErrorMessage(
			`NPIDL language server not found at ${npidlPath}. Set npidl.lsp.path to your npidl binary.`
		);
	}

	const gen = ++startGeneration;
	const next = createClient(npidlPath);
	await next.start();
	if (gen !== startGeneration) {
		await next.stop();
		return;
	}
	client = next;
	currentPath = npidlPath;
	console.log(`Using NPIDL Language Server at: ${npidlPath}`);
}

async function restartClient(npidlPath: string): Promise<void> {
	const previous = client;
	client = undefined;
	currentPath = undefined;
	if (previous) {
		try {
			await previous.stop();
		} catch (error) {
			console.error('Failed to stop NPIDL language server', error);
		}
	}
	await startClient(npidlPath);
}

export function activate(context: ExtensionContext) {
	const debugPositionsCmd = commands.registerCommand('npidl.debugPositions', async () => {
		const editor = window.activeTextEditor;
		if (!editor) {
			window.showErrorMessage('No active editor');
			return;
		}
		if (!client) {
			window.showErrorMessage('NPIDL language server is not running');
			return;
		}

		const uri = editor.document.uri.toString();

		try {
			const result = await client.sendRequest('npidl/debugPositions', {
				uri: uri
			});

			const doc = await workspace.openTextDocument({
				content: result as string,
				language: 'plaintext'
			});
			await window.showTextDocument(doc);
		} catch (error) {
			window.showErrorMessage(`Debug positions failed: ${error}`);
		}
	});

	const configWatcher = workspace.onDidChangeConfiguration(async (event) => {
		if (!event.affectsConfiguration('npidl.lsp.path')) {
			return;
		}
		const nextPath = getNpidlPath();
		if (nextPath === currentPath) {
			return;
		}
		try {
			await restartClient(nextPath);
			window.showInformationMessage(`NPIDL language server restarted: ${nextPath}`);
		} catch (error) {
			window.showErrorMessage(`Failed to restart NPIDL language server: ${error}`);
		}
	});

	context.subscriptions.push(debugPositionsCmd, configWatcher);
	void startClient(getNpidlPath());
}

export function deactivate(): Thenable<void> | undefined {
	startGeneration++;
	if (!client) {
		return undefined;
	}
	const previous = client;
	client = undefined;
	return previous.stop();
}
