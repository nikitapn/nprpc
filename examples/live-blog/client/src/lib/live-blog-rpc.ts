import * as NPRPC from 'nprpc';

import {
	BlogService,
	type AuthorPreview,
	type Comment,
	type PostDetail,
	type PostPage,
	type PostPreview
} from '../rpc/live_blog';

type HostInfo = {
	secured: boolean;
	webtransport?: boolean;
	webtransport_options?: unknown;
	objects: Record<string, NPRPC.ObjectProxy>;
};

const HOST_INFO_PATH = '/host.json';

let blogServicePromise: Promise<BlogService> | undefined;

const bigIntReviver = (key: string, value: unknown): unknown => {
	if (key === 'object_id' && typeof value === 'string') {
		return BigInt(value);
	}

	return value;
};

async function fetchHostInfo(): Promise<HostInfo> {
	const response = await fetch(HOST_INFO_PATH, { cache: 'no-store' });
	if (!response.ok) {
		throw new Error(`Failed to load ${HOST_INFO_PATH}: ${response.status} ${response.statusText}`);
	}

	return JSON.parse(await response.text(), bigIntReviver) as HostInfo;
}

export async function getBlogService(): Promise<BlogService> {
	if (!blogServicePromise) {
		blogServicePromise = (async () => {
			const hostInfo = await fetchHostInfo();
			const rpc = await NPRPC.init(false, hostInfo as never);
			const blog = NPRPC.narrow(rpc.host_info.objects.blog, BlogService);
			if (!blog) {
				throw new Error('host.json did not expose a valid blog service');
			}

			return blog;
		})();
	}

	return blogServicePromise;
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

export function formatError(error: unknown): string {
	if (error instanceof Error) {
		return error.message;
	}

	return String(error);
}

export type { AuthorPreview, Comment, PostDetail, PostPage, PostPreview };