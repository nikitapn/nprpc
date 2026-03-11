<script lang="ts">
	import '../app.css';
</script>

<div class="min-h-screen px-4 py-6 md:px-8 md:py-10">
	<div class="mx-auto flex max-w-7xl flex-col gap-6">
		<header class="glass-card flex flex-col gap-4 px-6 py-5 md:flex-row md:items-center md:justify-between">
			<div>
				<a href="/" class="text-2xl font-semibold tracking-tight text-stone-950">Live Blog</a>
				<p class="mt-1 text-sm text-stone-600">One Svelte shell, NPRPC data + streams, backend implemented in Swift or C++.</p>
			</div>
			<nav class="flex flex-wrap gap-2 text-sm font-medium text-stone-700">
				<a class="rounded-full border border-amber-900/15 px-4 py-2 hover:bg-white/70" href="/blog?page=1">Blog</a>
				<a class="rounded-full border border-amber-900/15 px-4 py-2 hover:bg-white/70" href="/post/first-steps-with-nprpc">Post</a>
				<a class="rounded-full border border-amber-900/15 px-4 py-2 hover:bg-white/70" href="/author/editorial-desk">Author</a>
			</nav>
		</header>

		<main>
			<slot />
		</main>
	</div>
</div>