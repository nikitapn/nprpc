<script lang="ts">
	import { page } from '$app/state';
	import ShellHero from '$lib/components/ShellHero.svelte';

	const slug = $derived(page.params.slug);
</script>

<div class="grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
	<div class="space-y-6">
		<ShellHero
			badge="Post page"
			title={slug}
			description="The SSR shell knows the slug and can frame the page immediately. The browser then performs getPost(slug) and listComments(postId, page) via typed RPC."
		/>

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

		<section class="glass-card p-8">
			<p class="eyebrow">Comments via RPC</p>
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