<script lang="ts">
	import { page } from '$app/state';
	import PostSkeletonList from '$lib/components/PostSkeletonList.svelte';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const slug = $derived(page.params.slug);
</script>

<div class="grid gap-6">
	<ShellHero
		badge="Author page"
		title={`Author: ${slug}`}
		description="Author pages can be shell-rendered immediately, then hydrated with getAuthor(slug) and listAuthorPosts(authorId, page) from Swift."
	/>

	<section class="grid gap-6 lg:grid-cols-[0.7fr_1.3fr]">
		<div class="glass-card p-8">
			<div class="skeleton h-24 w-24 rounded-full"></div>
			<div class="mt-5 space-y-3">
				<div class="skeleton h-7 w-2/3"></div>
				<div class="skeleton h-4 w-full"></div>
				<div class="skeleton h-4 w-5/6"></div>
			</div>
		</div>

		<PostSkeletonList pageNumber={1} />
	</section>
</div>