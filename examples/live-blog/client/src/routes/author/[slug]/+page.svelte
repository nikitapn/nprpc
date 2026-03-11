<script lang="ts">
	import { page } from '$app/state';
	import {
		formatError,
		formatIsoDate,
		loadAuthorPage,
		type AuthorPreview,
		type PostPreview
	} from '$lib/live-blog-rpc';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const slug = $derived(page.params.slug ?? '');

	let author = $state<AuthorPreview | null>(null);
	let posts = $state<PostPreview[]>([]);
	let isLoading = $state(true);
	let errorMessage = $state('');
	let requestVersion = 0;

	async function refreshAuthor(authorSlug: string) {
		const version = ++requestVersion;
		isLoading = true;
		errorMessage = '';

		try {
			const result = await loadAuthorPage(authorSlug, 1, 6);
			if (version !== requestVersion) return;
			author = result.author;
			posts = result.posts;
		} catch (error) {
			if (version !== requestVersion) return;
			author = null;
			posts = [];
			errorMessage = formatError(error);
		} finally {
			if (version === requestVersion) {
				isLoading = false;
			}
		}
	}

	$effect(() => {
		void refreshAuthor(slug);
	});
</script>

<div class="grid gap-6">
	<ShellHero
		badge="Author page"
		title={`Author: ${slug}`}
		description="Author pages are shell-rendered immediately, then hydrated with GetAuthor(slug) and ListAuthorPosts(slug, page) from the active NPRPC backend."
	/>

	<section class="grid gap-6 lg:grid-cols-[0.7fr_1.3fr]">
		<div class="glass-card p-8">
			{#if errorMessage}
				<p class="eyebrow">RPC error</p>
				<p class="mt-3 text-stone-700">{errorMessage}</p>
			{:else if isLoading && !author}
				<div class="skeleton h-24 w-24 rounded-full"></div>
				<div class="mt-5 space-y-3">
					<div class="skeleton h-7 w-2/3"></div>
					<div class="skeleton h-4 w-full"></div>
					<div class="skeleton h-4 w-5/6"></div>
				</div>
			{:else if author}
				<img class="h-24 w-24 rounded-full object-cover" src={author.avatar_url} alt={author.name} />
				<div class="mt-5 space-y-3">
					<h2 class="text-2xl font-semibold text-stone-950">{author.name}</h2>
					<p class="text-sm uppercase tracking-[0.22em] text-stone-500">{author.slug}</p>
					<p class="leading-7 text-stone-700">{author.bio}</p>
				</div>
			{/if}
		</div>

		<section class="space-y-4">
			<div class="flex items-center justify-between">
				<div>
					<p class="eyebrow">Author posts</p>
					<h2 class="mt-2 text-2xl font-semibold text-stone-900">Written from the shared demo repository</h2>
				</div>
				<div class="rounded-full border border-amber-900/15 bg-white/70 px-4 py-2 text-sm text-stone-600">
					{isLoading ? 'Loading' : `${posts.length} loaded`}
				</div>
			</div>

			<div class="grid gap-4">
				{#if isLoading && posts.length === 0}
					{#each Array.from({ length: 4 }) as _, index}
						<article class="glass-card p-6">
							<div class="mb-4 flex items-center justify-between">
								<div class="skeleton h-4 w-24"></div>
								<div class="skeleton h-4 w-16"></div>
							</div>
							<div class="skeleton h-8 w-3/5"></div>
							<div class="mt-4 space-y-3">
								<div class="skeleton h-4 w-full"></div>
								<div class="skeleton h-4 w-11/12"></div>
							</div>
							<div class="mt-6 text-xs uppercase tracking-[0.2em] text-stone-500">slot {index + 1}</div>
						</article>
					{/each}
				{:else}
					{#each posts as post}
						<article class="glass-card p-6">
							<div class="flex flex-wrap items-center justify-between gap-3 text-sm text-stone-500">
								<span>{formatIsoDate(post.published_at)}</span>
								<span>post #{post.id.toString()}</span>
							</div>
							<h3 class="mt-3 text-2xl font-semibold text-stone-950">
								<a class="transition hover:text-amber-800" href={`/post/${post.slug}`}>{post.title}</a>
							</h3>
							<p class="mt-3 leading-7 text-stone-700">{post.excerpt}</p>
						</article>
					{/each}
				{/if}
			</div>
		</section>
	</section>
</div>