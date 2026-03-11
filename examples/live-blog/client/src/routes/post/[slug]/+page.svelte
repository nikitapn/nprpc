<script lang="ts">
	import { page } from '$app/state';
	import {
		formatChatTimestamp,
		formatError,
		formatIsoDate,
		joinPostChat,
		loadPostPage,
		PresenceEventKind,
		type ChatServerEvent,
		type Comment,
		type PostDetail
	} from '$lib/live-blog-rpc';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const slug = $derived(page.params.slug ?? '');
	type ChatEntry = {
		id: string;
		kind: 'message' | 'status';
		author: string;
		body: string;
		createdAt: string;
	};

	let post = $state<PostDetail | null>(null);
	let comments = $state<Comment[]>([]);
	let isLoading = $state(true);
	let errorMessage = $state('');
	let requestVersion = 0;
	let chatUserName = $state('browser-demo');
	let chatDraft = $state('');
	let chatEntries = $state<ChatEntry[]>([]);
	let chatErrorMessage = $state('');
	let isChatConnecting = $state(false);
	let isChatConnected = $state(false);
	let chatGeneration = 0;
	let activeChatStream: Awaited<ReturnType<typeof joinPostChat>> | null = null;

	async function refreshPost(currentSlug: string) {
		const version = ++requestVersion;
		isLoading = true;
		errorMessage = '';

		try {
			const result = await loadPostPage(currentSlug);
			if (version !== requestVersion) return;
			post = result.post;
			comments = result.comments;
		} catch (error) {
			if (version !== requestVersion) return;
			post = null;
			comments = [];
			errorMessage = formatError(error);
		} finally {
			if (version === requestVersion) {
				isLoading = false;
			}
		}
	}

	function appendChatEntry(entry: ChatEntry) {
		chatEntries = [...chatEntries, entry];
	}

	function appendServerEvent(event: ChatServerEvent) {
		if (event.message.body) {
			appendChatEntry({
				id: crypto.randomUUID(),
				kind: 'message',
				author: event.message.author || 'system',
				body: event.message.body,
				createdAt: event.message.created_at
			});
		}

		if (event.presence.kind === PresenceEventKind.Joined || event.presence.kind === PresenceEventKind.Left) {
			appendChatEntry({
				id: crypto.randomUUID(),
				kind: 'status',
				author: 'presence',
				body:
					event.presence.kind === PresenceEventKind.Joined
						? `${event.presence.user_name} joined the room`
						: `${event.presence.user_name} left the room`,
				createdAt: event.message.created_at
			});
		}
	}

	function disconnectChat(resetEntries = false) {
		chatGeneration += 1;
		isChatConnected = false;
		isChatConnecting = false;
		chatErrorMessage = '';

		if (activeChatStream) {
			try {
				activeChatStream.writer.cancel();
			} catch {}
			try {
				activeChatStream.reader.cancel();
			} catch {}
			activeChatStream = null;
		}

		if (resetEntries) {
			chatEntries = [];
		}
	}

	async function connectChat() {
		if (!post || isChatConnecting) {
			return;
		}

		const userName = chatUserName.trim();
		if (!userName) {
			chatErrorMessage = 'Choose a display name before joining the stream.';
			return;
		}

		disconnectChat(false);
		const generation = chatGeneration;
		isChatConnecting = true;
		chatErrorMessage = '';

		try {
			const stream = await joinPostChat(post.id, userName);
			if (generation !== chatGeneration) {
				stream.writer.cancel();
				stream.reader.cancel();
				return;
			}

			activeChatStream = stream;
			isChatConnected = true;
			isChatConnecting = false;

			void (async () => {
				try {
					for await (const event of stream.reader) {
						if (generation !== chatGeneration) {
							break;
						}
						appendServerEvent(event);
					}

					if (generation === chatGeneration) {
						isChatConnected = false;
						activeChatStream = null;
					}
				} catch (error) {
					if (generation !== chatGeneration) {
						return;
					}

					isChatConnected = false;
					activeChatStream = null;
					chatErrorMessage = formatError(error);
				}
			})();
		} catch (error) {
			if (generation !== chatGeneration) {
				return;
			}

			isChatConnecting = false;
			isChatConnected = false;
			chatErrorMessage = formatError(error);
		}
	}

	function leaveChat() {
		disconnectChat(false);
	}

	function sendChatMessage() {
		const body = chatDraft.trim();
		if (!body || !activeChatStream || !isChatConnected) {
			return;
		}

		const outgoing = {
			author: chatUserName.trim() || 'browser-demo',
			body,
			created_at: new Date().toISOString()
		};

		activeChatStream.writer.write(outgoing);
		chatDraft = '';
	}

	$effect(() => {
		void refreshPost(slug);
	});

	$effect(() => {
		const currentPostId = post?.id;
		chatEntries = [];
		chatDraft = '';
		chatErrorMessage = '';
		disconnectChat(false);

		return () => {
			disconnectChat(false);
		};
	});
</script>

<div class="grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
	<div class="space-y-6">
		<ShellHero
			badge="Post page"
			title={slug}
			description="The SSR shell knows the slug and frames the page immediately. After hydration, the browser performs GetPost(slug) and ListComments(postId, page) against the active NPRPC backend."
		/>

		{#if errorMessage}
			<section class="glass-card p-8 text-stone-700">
				<p class="eyebrow">RPC error</p>
				<p class="mt-3">{errorMessage}</p>
			</section>
		{:else if isLoading && !post}
			<article class="glass-card p-8">
				<div class="skeleton h-4 w-28"></div>
				<div class="mt-5 space-y-4">
					<div class="skeleton h-10 w-4/5"></div>
					<div class="skeleton h-4 w-full"></div>
					<div class="skeleton h-4 w-11/12"></div>
					<div class="skeleton h-4 w-10/12"></div>
					<div class="skeleton h-4 w-8/12"></div>
				</div>
			</article>
		{:else if post}
			<article class="glass-card p-8">
				<p class="eyebrow">{formatIsoDate(post.published_at)}</p>
				<h2 class="mt-4 text-4xl font-semibold tracking-tight text-stone-950">{post.title}</h2>
				<p class="mt-4 max-w-3xl text-lg leading-8 text-stone-700">{post.summary}</p>
				<div class="mt-5 flex flex-wrap items-center gap-3 text-sm text-stone-500">
					<a class="rounded-full border border-amber-900/15 px-3 py-1 hover:bg-white/80" href={`/author/${post.author.slug}`}>{post.author.name}</a>
					{#each post.tags as tag}
						<span class="rounded-full bg-amber-100 px-3 py-1 text-amber-900">{tag}</span>
					{/each}
				</div>
				<div class="prose prose-stone mt-8 max-w-none leading-8 text-stone-800">
					{@html post.body_html}
				</div>
			</article>
		{/if}

		<section class="glass-card p-8">
			<p class="eyebrow">Comments via RPC</p>
			{#if isLoading && comments.length === 0}
				<div class="mt-5 space-y-4">
					{#each Array.from({ length: 3 }) as _}
						<div class="rounded-2xl border border-amber-900/10 bg-white/70 p-4">
							<div class="skeleton h-4 w-24"></div>
							<div class="mt-3 space-y-2">
								<div class="skeleton h-4 w-full"></div>
								<div class="skeleton h-4 w-4/5"></div>
							</div>
						</div>
					{/each}
				</div>
			{:else}
				<div class="mt-5 space-y-4">
					{#if comments.length === 0}
						<div class="rounded-2xl border border-dashed border-amber-900/20 bg-white/60 p-5 text-stone-600">
							No comments yet for this post.
						</div>
					{:else}
						{#each comments as comment}
							<div class="rounded-2xl border border-amber-900/10 bg-white/70 p-4">
								<div class="flex flex-wrap items-center justify-between gap-3 text-sm text-stone-500">
									<strong class="text-stone-900">{comment.author_name}</strong>
									<span>{formatIsoDate(comment.created_at)}</span>
								</div>
								<p class="mt-3 leading-7 text-stone-700">{comment.body}</p>
							</div>
						{/each}
					{/if}
				</div>
			{/if}
		</section>
	</div>

	<aside class="space-y-6">
		<section class="glass-card p-7">
			<p class="eyebrow">Live chat via bidi stream</p>
			<p class="mt-3 text-sm leading-6 text-stone-600">
				This connects to <code>JoinPostChat(post_id, user_name)</code>, keeps the bidi stream open, sends chat envelopes from the browser, and renders server events back into the page.
			</p>

			<div class="mt-5 grid gap-3">
				<label class="grid gap-2 text-sm text-stone-700">
					<span class="font-medium text-stone-900">Display name</span>
					<input
						bind:value={chatUserName}
						class="rounded-2xl border border-amber-900/15 bg-white/85 px-4 py-3 outline-none transition focus:border-amber-500"
						placeholder="browser-demo"
						disabled={isChatConnected || isChatConnecting}
					/>
				</label>

				<div class="flex flex-wrap gap-3">
					<button
						type="button"
						class="rounded-full bg-stone-950 px-4 py-2 text-sm font-medium text-stone-50 transition hover:bg-stone-800 disabled:cursor-not-allowed disabled:bg-stone-400"
						onclick={connectChat}
						disabled={!post || isChatConnected || isChatConnecting}
					>
						{isChatConnecting ? 'Connecting...' : isChatConnected ? 'Connected' : 'Join stream'}
					</button>
					<button
						type="button"
						class="rounded-full border border-amber-900/15 px-4 py-2 text-sm font-medium text-stone-700 transition hover:bg-white/80 disabled:cursor-not-allowed disabled:opacity-50"
						onclick={leaveChat}
						disabled={!isChatConnected && !isChatConnecting}
					>
						Leave
					</button>
				</div>

				{#if chatErrorMessage}
					<div class="rounded-2xl border border-rose-200 bg-rose-50 px-4 py-3 text-sm text-rose-700">
						{chatErrorMessage}
					</div>
				{/if}

				<div class="rounded-[28px] bg-stone-950 p-4 text-stone-100">
					<div class="flex items-center justify-between gap-3 border-b border-stone-800 pb-3 text-xs uppercase tracking-[0.24em] text-stone-400">
						<span>{isChatConnected ? 'Stream live' : 'Disconnected'}</span>
						<span>{post ? `post #${post.id}` : 'waiting for post'}</span>
					</div>
					<div class="mt-4 space-y-3">
						{#if chatEntries.length === 0}
							<div class="rounded-2xl border border-dashed border-stone-700 px-4 py-5 text-sm leading-6 text-stone-400">
								Join the stream to see server events and echoed messages for this post.
							</div>
						{:else}
							{#each chatEntries as entry}
								{#if entry.kind === 'status'}
									<div class="rounded-2xl border border-stone-800 bg-stone-900/70 px-4 py-3 text-sm text-amber-200">
										<div class="flex items-center justify-between gap-3">
											<span>{entry.body}</span>
											<span class="text-xs uppercase tracking-[0.2em] text-stone-500">{formatChatTimestamp(entry.createdAt)}</span>
										</div>
									</div>
								{:else}
									<div class="rounded-2xl bg-stone-900 px-4 py-4">
										<div class="flex items-center justify-between gap-3 text-xs uppercase tracking-[0.2em] text-stone-400">
											<strong class="text-stone-100">{entry.author}</strong>
											<span>{formatChatTimestamp(entry.createdAt)}</span>
										</div>
										<p class="mt-3 text-sm leading-6 text-stone-200">{entry.body}</p>
									</div>
								{/if}
							{/each}
						{/if}
					</div>
				</div>

				<div class="grid gap-3">
					<label class="grid gap-2 text-sm text-stone-700">
						<span class="font-medium text-stone-900">Send a message</span>
						<textarea
							bind:value={chatDraft}
							class="min-h-28 rounded-2xl border border-amber-900/15 bg-white/85 px-4 py-3 outline-none transition focus:border-amber-500 disabled:cursor-not-allowed disabled:opacity-60"
							placeholder="Type a message to send over the bidi stream"
							disabled={!isChatConnected}
						></textarea>
					</label>
					<button
						type="button"
						class="rounded-full bg-amber-600 px-4 py-2 text-sm font-medium text-white transition hover:bg-amber-500 disabled:cursor-not-allowed disabled:bg-amber-300"
						onclick={sendChatMessage}
						disabled={!isChatConnected || !chatDraft.trim()}
					>
						Send over bidi stream
					</button>
				</div>
			</div>
		</section>

		<section class="glass-card p-7">
			<p class="eyebrow">Media later</p>
			<div class="mt-5 aspect-video rounded-[22px] border border-amber-900/10 bg-stone-900/90 p-5 text-sm text-stone-300">
				Reserved for server-side media streaming after the core blog + chat flow is in place.
			</div>
		</section>
	</aside>
</div>