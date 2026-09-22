// NPRPC access for the browser islands.
//
// The blog, post and author pages are rendered by the backend, so nothing here
// fetches content any more. What remains is the two services that need a live
// connection: chat (bidi stream) and media (server stream).

import * as NPRPC from 'nprpc';

import {
	ChatService,
	MediaService,
	type ChatEnvelope,
	type ChatServerEvent,
	PresenceEventKind
} from '../rpc/live_blog';

let chatServicePromise: Promise<ChatService> | undefined;
let mediaServicePromise: Promise<MediaService> | undefined;

export async function getChatService(): Promise<ChatService> {
	if (!chatServicePromise) {
		chatServicePromise = (async () => {
			const rpc = await NPRPC.init();
			const chat = NPRPC.narrow(rpc.host_info.objects.chat, ChatService);
			if (!chat) {
				throw new Error('host.json did not expose a valid chat service');
			}

			return chat;
		})();
	}

	return chatServicePromise;
}

export async function getMediaService(): Promise<MediaService> {
	if (!mediaServicePromise) {
		mediaServicePromise = (async () => {
			const rpc = await NPRPC.init();
			const media = NPRPC.narrow(rpc.host_info.objects.media, MediaService);
			if (!media) {
				throw new Error('host.json did not expose a valid media service');
			}

			return media;
		})();
	}

	return mediaServicePromise;
}

export async function joinPostChat(
	postId: bigint,
	userName: string
): Promise<Awaited<ReturnType<ChatService['JoinPostChat']>>> {
	const chat = await getChatService();
	return chat.JoinPostChat(postId, userName);
}

export async function streamPostVideo(
	postId: bigint
): Promise<Awaited<ReturnType<MediaService['OpenPostVideo']>>> {
	const media = await getMediaService();
	return media.OpenPostVideo(postId);
}

export function formatChatTimestamp(value: string): string {
	if (!value) {
		return 'now';
	}

	const parsed = new Date(value);
	if (Number.isNaN(parsed.getTime())) {
		return value;
	}

	return parsed.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

export function formatError(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

export { PresenceEventKind };
export type { ChatEnvelope, ChatServerEvent };
