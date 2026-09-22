// Live chat island — a bidi stream to ChatService.JoinPostChat.
//
// Mounts on `[data-nprpc-chat]`, which post.mustache emits with the post id.

import {
	formatChatTimestamp,
	formatError,
	joinPostChat,
	PresenceEventKind,
	type ChatServerEvent
} from '../lib/live-blog-rpc';
import { clear, el, mountAll } from './dom';

type ActiveStream = Awaited<ReturnType<typeof joinPostChat>>;

const FIELD =
	'rounded-2xl border border-amber-900/15 bg-white/85 px-4 py-3 outline-none transition focus:border-amber-500 disabled:cursor-not-allowed disabled:opacity-60';

function setupChat(root: HTMLElement, postId: bigint): void {
	// The template ships a heading and a <noscript>; replace the lot.
	clear(root);

	const nameInput = el('input', {
		class: FIELD,
		value: 'browser-demo',
		placeholder: 'browser-demo'
	});

	const joinButton = el('button', {
		type: 'button',
		class:
			'rounded-full bg-stone-950 px-4 py-2 text-sm font-medium text-stone-50 transition hover:bg-stone-800 disabled:cursor-not-allowed disabled:bg-stone-400'
	}, ['Join stream']);

	const leaveButton = el('button', {
		type: 'button',
		class:
			'rounded-full border border-amber-900/15 px-4 py-2 text-sm font-medium text-stone-700 transition hover:bg-white/80 disabled:cursor-not-allowed disabled:opacity-50',
		disabled: true
	}, ['Leave']);

	const errorBox = el('div', {
		class: 'rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700',
		hidden: true
	});

	const statusLabel = el('span', {}, ['Disconnected']);
	const entries = el('div', { class: 'mt-4 space-y-3' });

	const draft = el('textarea', {
		class: `min-h-28 ${FIELD}`,
		placeholder: 'Type a message to send over the bidi stream',
		disabled: true
	});

	const sendButton = el('button', {
		type: 'button',
		class:
			'rounded-full bg-amber-600 px-4 py-2 text-sm font-medium text-white transition hover:bg-amber-500 disabled:cursor-not-allowed disabled:bg-amber-300',
		disabled: true
	}, ['Send over bidi stream']);

	root.append(
		el('p', { class: 'eyebrow' }, ['Live chat via bidi stream']),
		el('p', { class: 'mt-3 text-sm leading-6 text-stone-600' }, [
			'Connects to ',
			el('code', {}, ['JoinPostChat(post_id, user_name)']),
			', keeps the bidi stream open, sends envelopes from the browser, and renders server events back into the page.'
		]),
		el('div', { class: 'mt-5 grid gap-3' }, [
			el('label', { class: 'grid gap-2 text-sm text-stone-700' }, [
				el('span', { class: 'font-medium text-stone-900' }, ['Display name']),
				nameInput
			]),
			el('div', { class: 'flex flex-wrap gap-3' }, [joinButton, leaveButton]),
			errorBox,
			el('div', { class: 'rounded-[28px] bg-stone-950 p-4 text-stone-100' }, [
				el('div', {
					class:
						'flex items-center justify-between gap-3 border-b border-stone-800 pb-3 text-xs uppercase tracking-[0.24em] text-stone-400'
				}, [statusLabel, el('span', {}, [`post #${postId}`])]),
				entries
			]),
			el('div', { class: 'grid gap-3' }, [
				el('label', { class: 'grid gap-2 text-sm text-stone-700' }, [
					el('span', { class: 'font-medium text-stone-900' }, ['Send a message']),
					draft
				]),
				sendButton
			])
		])
	);

	// A generation counter retires callbacks from a previous connection, so a
	// slow join that resolves after the user left cannot revive the UI.
	let generation = 0;
	let stream: ActiveStream | null = null;
	let connected = false;
	let connecting = false;

	function showPlaceholder(): void {
		clear(entries);
		entries.append(
			el('div', {
				class:
					'rounded-2xl border border-dashed border-stone-700 px-4 py-5 text-sm leading-6 text-stone-400'
			}, ['Join the stream to see server events and echoed messages for this post.'])
		);
	}

	function setError(message: string): void {
		errorBox.textContent = message;
		errorBox.hidden = message === '';
	}

	function syncControls(): void {
		nameInput.disabled = connected || connecting;
		joinButton.disabled = connected || connecting;
		joinButton.textContent = connecting ? 'Connecting...' : connected ? 'Connected' : 'Join stream';
		leaveButton.disabled = !connected && !connecting;
		draft.disabled = !connected;
		sendButton.disabled = !connected || draft.value.trim() === '';
		statusLabel.textContent = connected ? 'Stream live' : 'Disconnected';
	}

	function appendMessage(author: string, body: string, createdAt: string): void {
		if (entries.dataset.empty !== 'false') {
			clear(entries);
			entries.dataset.empty = 'false';
		}
		entries.append(
			el('div', { class: 'rounded-2xl bg-stone-900 px-4 py-4' }, [
				el('div', {
					class:
						'flex items-center justify-between gap-3 text-xs uppercase tracking-[0.2em] text-stone-400'
				}, [
					el('strong', { class: 'text-stone-100' }, [author || 'system']),
					el('span', {}, [formatChatTimestamp(createdAt)])
				]),
				el('p', { class: 'mt-3 text-sm leading-6 text-stone-200' }, [body])
			])
		);
		entries.lastElementChild?.scrollIntoView({ block: 'nearest' });
	}

	function appendStatus(body: string, createdAt: string): void {
		if (entries.dataset.empty !== 'false') {
			clear(entries);
			entries.dataset.empty = 'false';
		}
		entries.append(
			el('div', {
				class: 'rounded-2xl border border-stone-800 bg-stone-900/70 px-4 py-3 text-sm text-amber-200'
			}, [
				el('div', { class: 'flex items-center justify-between gap-3' }, [
					el('span', {}, [body]),
					el('span', { class: 'text-xs uppercase tracking-[0.2em] text-stone-500' }, [
						formatChatTimestamp(createdAt)
					])
				])
			])
		);
		entries.lastElementChild?.scrollIntoView({ block: 'nearest' });
	}

	function onServerEvent(event: ChatServerEvent): void {
		if (event.message.body) {
			appendMessage(event.message.author, event.message.body, event.message.created_at);
		}

		const kind = event.presence.kind;
		if (kind === PresenceEventKind.Joined || kind === PresenceEventKind.Left) {
			const verb = kind === PresenceEventKind.Joined ? 'joined' : 'left';
			appendStatus(`${event.presence.user_name} ${verb} the room`, event.message.created_at);
		}
	}

	function disconnect(): void {
		generation += 1;
		connected = false;
		connecting = false;
		setError('');

		if (stream) {
			try {
				stream.writer.cancel();
			} catch {
				/* already torn down */
			}
			try {
				stream.reader.cancel();
			} catch {
				/* already torn down */
			}
			stream = null;
		}
		syncControls();
	}

	async function connect(): Promise<void> {
		if (connecting) return;

		const userName = nameInput.value.trim();
		if (!userName) {
			setError('Choose a display name before joining the stream.');
			return;
		}

		disconnect();
		const mine = generation;
		connecting = true;
		setError('');
		syncControls();

		try {
			const opened = await joinPostChat(postId, userName);
			if (mine !== generation) {
				opened.writer.cancel();
				opened.reader.cancel();
				return;
			}

			stream = opened;
			connected = true;
			connecting = false;
			syncControls();

			void (async () => {
				try {
					for await (const event of opened.reader) {
						if (mine !== generation) break;
						onServerEvent(event);
					}
					if (mine === generation) {
						connected = false;
						stream = null;
						syncControls();
					}
				} catch (error) {
					if (mine !== generation) return;
					connected = false;
					stream = null;
					setError(formatError(error));
					syncControls();
				}
			})();
		} catch (error) {
			if (mine !== generation) return;
			connecting = false;
			connected = false;
			setError(formatError(error));
			syncControls();
		}
	}

	function send(): void {
		const body = draft.value.trim();
		if (!body || !stream || !connected) return;

		stream.writer.write({
			author: nameInput.value.trim() || 'browser-demo',
			body,
			created_at: new Date().toISOString()
		});
		draft.value = '';
		syncControls();
	}

	joinButton.addEventListener('click', () => void connect());
	leaveButton.addEventListener('click', () => disconnect());
	sendButton.addEventListener('click', () => send());
	draft.addEventListener('input', () => syncControls());
	draft.addEventListener('keydown', (event) => {
		// Enter sends; Shift+Enter is a newline.
		if (event.key === 'Enter' && !event.shiftKey) {
			event.preventDefault();
			send();
		}
	});
	// Leaving the page mid-stream should close it, not leak a session server-side.
	window.addEventListener('pagehide', () => disconnect());

	showPlaceholder();
	syncControls();
}

export function mountChat(): void {
	mountAll('[data-nprpc-chat]', setupChat);
}
