import * as NPRPC from 'nprpc';

import {
	BlogService,
	ChatService,
	MediaService,
	type AuthorPreview,
	type ChatEnvelope,
	type ChatServerEvent,
	type Comment,
	type PostDetail,
	type PostPage,
	type PostPreview,
	PresenceEventKind
} from '../rpc/live_blog';

let blogServicePromise: Promise<BlogService> | undefined;
let chatServicePromise: Promise<ChatService> | undefined;
let mediaServicePromise: Promise<MediaService> | undefined;

export async function getBlogService(): Promise<BlogService> {
	if (!blogServicePromise) {
		blogServicePromise = (async () => {
			const rpc = await NPRPC.init();
			const blog = NPRPC.narrow(rpc.host_info.objects.blog, BlogService);
			if (!blog) {
				throw new Error('host.json did not expose a valid blog service');
			}

			return blog;
		})();
	}

	return blogServicePromise;
}

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

export async function loadBlogPage(page: number, pageSize: number): Promise<PostPage> {
	const blog = await getBlogService();
	return blog.ListPosts(page, pageSize);
}

export async function loadPostPage(slug: string, commentPageSize = 6): Promise<{ post: PostDetail; comments: Comment[] }> {
	const blog = await getBlogService();
	const post = await blog.GetPost(slug);
	const comments = await blog.ListComments(post.id, 1, commentPageSize);
	return { post, comments };
}

export async function loadAuthorPage(
	authorSlug: string,
	page: number,
	pageSize: number
): Promise<{ author: AuthorPreview; posts: PostPreview[] }> {
	const blog = await getBlogService();
	const [author, posts] = await Promise.all([
		blog.GetAuthor(authorSlug),
		blog.ListAuthorPosts(authorSlug, page, pageSize)
	]);

	return { author, posts };
}

export async function joinPostChat(postId: bigint, userName: string): Promise<Awaited<ReturnType<ChatService['JoinPostChat']>>> {
	const chat = await getChatService();
	return chat.JoinPostChat(postId, userName);
}

export async function streamPostVideo(postId: bigint): Promise<Awaited<ReturnType<MediaService['OpenPostVideo']>>> {
	const media = await getMediaService();
	return media.OpenPostVideo(postId);
}

export async function getVideoDashManifest(postId: bigint): Promise<string> {
	const media = await getMediaService();
	return media.GetVideoDashManifest(postId);
}

export async function getVideoDashSegmentRange(
	postId: bigint,
	byteOffset: bigint,
	byteLength: bigint
): Promise<Awaited<ReturnType<MediaService['GetVideoDashSegmentRange']>>> {
	const media = await getMediaService();
	return media.GetVideoDashSegmentRange(postId, byteOffset, byteLength);
}

export function formatIsoDate(value: string): string {
	if (!value) {
		return 'Unscheduled';
	}

	const parsed = new Date(value);
	if (Number.isNaN(parsed.getTime())) {
		return value;
	}

	return parsed.toLocaleDateString(undefined, {
		day: 'numeric',
		month: 'short',
		year: 'numeric'
	});
}

export function formatChatTimestamp(value: string): string {
	if (!value) {
		return 'now';
	}

	const parsed = new Date(value);
	if (Number.isNaN(parsed.getTime())) {
		return value;
	}

	return parsed.toLocaleTimeString([], {
		hour: '2-digit',
		minute: '2-digit'
	});
}

export function formatError(error: unknown): string {
	if (error instanceof Error) {
		return error.message;
	}

	return String(error);
}

export { PresenceEventKind };
export type { AuthorPreview, ChatEnvelope, ChatServerEvent, Comment, PostDetail, PostPage, PostPreview };