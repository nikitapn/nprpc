<script lang="ts">
	export let pageNumber = 1;
</script>

<section class="space-y-4">
	<div class="flex items-center justify-between">
		<div>
			<p class="eyebrow">Page {pageNumber}</p>
			<h2 class="mt-2 text-2xl font-semibold text-stone-900">Latest posts hydrate from Swift RPC</h2>
		</div>
		<div class="rounded-full border border-amber-900/15 bg-white/70 px-4 py-2 text-sm text-stone-600">
			Skeleton SSR shell
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