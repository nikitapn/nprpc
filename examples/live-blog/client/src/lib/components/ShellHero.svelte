<script lang="ts">
	export let title: string;
	export let description: string;
	export let badge: string;
</script>

<section class="glass-card grain overflow-hidden p-8 md:p-12">
	<div class="grid gap-8 md:grid-cols-[1.5fr_0.9fr] md:items-end">
		<div class="space-y-5">
			<p class="eyebrow">{badge}</p>
			<h1 class="max-w-3xl text-4xl font-semibold tracking-tight text-stone-900 md:text-6xl">
				{title}
			</h1>
			<p class="max-w-2xl text-lg leading-8 text-stone-700 md:text-xl">
				{description}
			</p>
		</div>

		<div class="rounded-[28px] border border-amber-800/15 bg-stone-950 p-5 text-stone-50 shadow-2xl">
			<div class="mb-4 flex items-center justify-between text-xs uppercase tracking-[0.24em] text-stone-400">
				<span>Hybrid SSR</span>
				<span>Swift RPC</span>
			</div>
			<div class="space-y-3 text-sm leading-6 text-stone-300">
				<p>
					Shell first. Data after hydration. One backend for posts, comments, chat, and media.
				</p>
				<div class="grid grid-cols-3 gap-2 text-center text-xs font-medium">
					<div class="rounded-full bg-stone-900 px-3 py-2">Blog</div>
					<div class="rounded-full bg-amber-700/80 px-3 py-2">Chat</div>
					<div class="rounded-full bg-stone-800 px-3 py-2">Video</div>
				</div>
			</div>
		</div>
	</div>
</section>