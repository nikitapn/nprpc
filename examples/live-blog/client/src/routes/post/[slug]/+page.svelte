<script lang="ts">
	import { page } from '$app/state';
	import { formatError, formatIsoDate, loadPostPage, type Comment, type PostDetail } from '$lib/live-blog-rpc';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const slug = $derived(page.params.slug ?? '');

	let post = $state<PostDetail | null>(null);
	let comments = $state<Comment[]>([]);
	let isLoading = $state(true);
	let errorMessage = $state('');
	let requestVersion = 0;

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

	$effect(() => {
		void refreshPost(slug);
	});
</script>

<div class="grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
	<div class="space-y-6">
		<ShellHero
			badge="Post page"
			title={slug}
			description="The SSR shell knows the slug and frames the page immediately. After hydration, the browser performs GetPost(slug) and ListComments(postId, page) against the Swift backend."
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
			<div class="mt-5 space-y-3">
				{#each Array.from({ length: 4 }) as _}
					<div class="rounded-2xl bg-stone-950 p-4 text-stone-100">
						<div class="skeleton h-3 w-20 bg-stone-700"></div>
						<div class="mt-3 skeleton h-4 w-10/12 bg-stone-700"></div>
					</div>
				{/each}
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