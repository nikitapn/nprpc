<script lang="ts">
	import { page } from '$app/state';
	import PostSkeletonList from '$lib/components/PostSkeletonList.svelte';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const pageNumber = $derived(Number(page.url.searchParams.get('page') ?? '1'));
</script>

<div class="grid gap-6">
	<ShellHero
		badge="Blog listing"
		title={`/blog?page=${pageNumber}`}
		description="SSR already knows the requested page number and can render the correct shell. After hydration, the client should call BlogService.listPosts(page, pageSize) on the Swift backend."
	/>

	<PostSkeletonList {pageNumber} />
</div>