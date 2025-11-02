import * as path from 'path';
import { workspace, ExtensionContext } from 'vscode';

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
	const npidlPath = config.get<string>('lsp.path', 'npidl');

	// Server options - launch npidl with --lsp flag
	const serverOptions: ServerOptions = {
		command: npidlPath,
		args: ['--lsp'],
		transport: TransportKind.stdio
	};

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

	// Start the client (also starts the server)
	client.start();
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
