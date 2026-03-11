<script lang="ts">
	import { page } from '$app/state';
	import ShellHero from '$lib/components/ShellHero.svelte';
	import {
		formatError,
		formatIsoDate,
		loadBlogPage,
		type PostPage
	} from '$lib/live-blog-rpc';

	const pageNumber = $derived(Number(page.url.searchParams.get('page') ?? '1'));

	let postPage = $state<PostPage | null>(null);
	let isLoading = $state(true);
	let errorMessage = $state('');
	let requestVersion = 0;

	async function refreshPosts(currentPage: number) {
		const version = ++requestVersion;
		isLoading = true;
		errorMessage = '';

		try {
			const nextPage = await loadBlogPage(currentPage, 5);
			if (version !== requestVersion) return;
			postPage = nextPage;
		} catch (error) {
			if (version !== requestVersion) return;
			postPage = null;
			errorMessage = formatError(error);
		} finally {
			if (version === requestVersion) {
				isLoading = false;
			}
		}
	}

	$effect(() => {
		void refreshPosts(pageNumber);
	});
</script>

<div class="grid gap-6">
	<ShellHero
		badge="Blog listing"
		title={`/blog?page=${pageNumber}`}
		description="SSR already knows the requested page number and can render the correct shell. After hydration, this page performs a real BlogService.ListPosts(page, pageSize) call against the active NPRPC backend."
	/>

	{#if errorMessage}
		<section class="glass-card p-6 text-stone-700">
			<p class="eyebrow">RPC error</p>
			<p class="mt-3 text-base">{errorMessage}</p>
		</section>
	{:else if isLoading && !postPage}
		<section class="space-y-4">
			<div class="flex items-center justify-between">
				<div>
					<p class="eyebrow">Page {pageNumber}</p>
					<h2 class="mt-2 text-2xl font-semibold text-stone-900">Latest posts hydrate from NPRPC RPC</h2>
				</div>
				<div class="rounded-full border border-amber-900/15 bg-white/70 px-4 py-2 text-sm text-stone-600">
					Loading
				</div>
			</div>

			<div class="grid gap-4">
				{#each Array.from({ length: 5 }) as _, index}
					<article class="glass-card p-6">
						<div class="mb-4 flex items-center justify-between">
							<div class="skeleton h-4 w-24"></div>
							<div class="skeleton h-4 w-16"></div>
						</div>
						<div class="skeleton h-8 w-3/5"></div>
						<div class="mt-4 space-y-3">
							<div class="skeleton h-4 w-full"></div>
							<div class="skeleton h-4 w-11/12"></div>
							<div class="skeleton h-4 w-8/12"></div>
						</div>
						<div class="mt-6 flex gap-2 text-xs uppercase tracking-[0.2em] text-stone-500">
							<span>slot {index + 1}</span>
							<span>rpc pending</span>
						</div>
					</article>
				{/each}
			</div>
		</section>
	{:else if postPage}
		<section class="space-y-4">
			<div class="flex items-center justify-between">
				<div>
					<p class="eyebrow">Page {postPage.page}</p>
					<h2 class="mt-2 text-2xl font-semibold text-stone-900">{postPage.total_posts} posts available in the active backend</h2>
				</div>
				<div class="rounded-full border border-amber-900/15 bg-white/70 px-4 py-2 text-sm text-stone-600">
					Hydrated from RPC
				</div>
			</div>

			<div class="grid gap-4">
				{#each postPage.posts as post}
					<article class="glass-card overflow-hidden">
						{#if post.cover_url}
							<div class="h-44 w-full bg-cover bg-center" style={`background-image: url('${post.cover_url}')`}></div>
						{/if}
						<div class="p-6">
							<div class="flex flex-wrap items-center justify-between gap-3 text-sm text-stone-600">
								<span>{post.author.name}</span>
								<span>{formatIsoDate(post.published_at)}</span>
							</div>
							<h3 class="mt-3 text-2xl font-semibold text-stone-950">
								<a class="transition hover:text-amber-800" href={`/post/${post.slug}`}>{post.title}</a>
							</h3>
							<p class="mt-3 text-base leading-7 text-stone-700">{post.excerpt}</p>
							<div class="mt-5 flex flex-wrap items-center gap-3 text-sm text-stone-500">
								<a class="rounded-full border border-amber-900/15 px-3 py-1 hover:bg-white/80" href={`/author/${post.author.slug}`}>{post.author.slug}</a>
								<span>post #{post.id.toString()}</span>
							</div>
						</div>
					</article>
				{/each}
			</div>
		</section>
	{/if}
</div>