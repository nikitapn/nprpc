import * as path from 'path';
import { workspace, ExtensionContext, commands, window } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
	TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
	// Get the npidl executable path from settings
	const config = workspace.getConfiguration('npidl');
	// const npidlPath = config.get<string>('lsp.path', '/home/nikita/projects/npsystem/build/bin/npidl');
	const npidlPath = '/home/nikita/projects/npsystem/build/linux/bin/npidl';

	// Server options - launch npidl with --lsp flag, redirect stderr to log file
	const serverOptions: ServerOptions = {
		command: 'bash',
		args: ['-c', `${npidlPath} --lsp 2>/tmp/npidl-vscode-stderr.log`],
		transport: TransportKind.stdio
	};

	console.log(`Using NPIDL Language Server at: ${npidlPath}`);

	// Client options - configure file patterns and synchronization
	const clientOptions: LanguageClientOptions = {
		documentSelector: [
			{ scheme: 'file', language: 'npidl' }
		],
		synchronize: {
			// Notify the server about file changes to .npidl files
			fileEvents: workspace.createFileSystemWatcher('**/*.npidl')
		}
	};

	// Create and start the language client
	client = new LanguageClient(
		'npidlLanguageServer',
		'NPIDL Language Server',
		serverOptions,
		clientOptions
	);

	// Register debug command
	const debugPositionsCmd = commands.registerCommand('npidl.debugPositions', async () => {
		const editor = window.activeTextEditor;
		if (!editor) {
			window.showErrorMessage('No active editor');
			return;
		}

		const uri = editor.document.uri.toString();
		
		try {
			const result = await client.sendRequest('npidl/debugPositions', {
				uri: uri
			});
			
			// Show result in a new document
			const doc = await workspace.openTextDocument({
				content: result as string,
				language: 'plaintext'
			});
			await window.showTextDocument(doc);
		} catch (error) {
			window.showErrorMessage(`Debug positions failed: ${error}`);
		}
	});

	context.subscriptions.push(debugPositionsCmd);

	// Start the client (also starts the server)
	client.start();
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
