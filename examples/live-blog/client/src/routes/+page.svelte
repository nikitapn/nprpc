<script lang="ts">
	import ShellHero from '$lib/components/ShellHero.svelte';
</script>

<div class="grid gap-6">
	<ShellHero
		badge="Example concept"
		title="A route-aware blog shell that hydrates from NPRPC backends in Swift or C++"
		description="The page frame is server-rendered by SvelteKit, while posts, comments, chat, and future media streams come from a typed NPRPC backend. The same client is shared by both server implementations."
	/>

	<section class="grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
		<div class="glass-card p-7">
			<p class="eyebrow">Flow</p>
			<div class="mt-4 space-y-4 text-stone-700">
				<p>1. SSR renders route-aware skeleton HTML.</p>
				<p>2. Browser creates NPRPC client objects for blog and chat services.</p>
				<p>3. Posts/comments load via regular RPC and chat attaches via bidi streams.</p>
				<p>4. Media streaming can be layered in later without changing the basic architecture.</p>
			</div>
		</div>

		<div class="glass-card p-7">
			<p class="eyebrow">Demo pages</p>
			<ul class="mt-4 space-y-3 text-stone-700">
				<li><a class="underline decoration-amber-700/40 underline-offset-4" href="/blog?page=10">/blog?page=10</a></li>
				<li><a class="underline decoration-amber-700/40 underline-offset-4" href="/post/first-steps-with-nprpc">/post/first-steps-with-nprpc</a></li>
				<li><a class="underline decoration-amber-700/40 underline-offset-4" href="/author/editorial-desk">/author/editorial-desk</a></li>
			</ul>
		</div>
	</section>
</div>