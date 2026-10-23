<!--
  A panel that opens under its button, with an arrow pointing at the button.
  Closes on a click outside or Escape (via `onclose`, so the owner can refuse,
  e.g. while an export is running). `align` picks which edge of the button the
  panel lines up with; use "right" near the right side of the screen.
-->
<script lang="ts">
	import type { Snippet } from 'svelte';

	let {
		open,
		onclose,
		align = 'left',
		width = 380,
		label,
		trigger,
		children,
	}: {
		open: boolean;
		onclose: () => void;
		align?: 'left' | 'right';
		width?: number;
		label: string;
		trigger: Snippet;
		children: Snippet;
	} = $props();

	let anchor: HTMLDivElement;
	let anchorWidth = $state(0);

	$effect(() => {
		if (!open) return;
		const outside = (e: PointerEvent) => {
			if (!anchor.contains(e.target as Node)) onclose();
		};
		const key = (e: KeyboardEvent) => {
			if (e.key === 'Escape') onclose();
		};
		// Next tick, so the click that opened it doesn't close it
		const t = setTimeout(() => document.addEventListener('pointerdown', outside));
		document.addEventListener('keydown', key);
		return () => {
			clearTimeout(t);
			document.removeEventListener('pointerdown', outside);
			document.removeEventListener('keydown', key);
		};
	});
</script>

<div class="anchor" bind:this={anchor} bind:clientWidth={anchorWidth}>
	{@render trigger()}
	{#if open}
		<div
			class="pop {align}"
			role="dialog"
			aria-label={label}
			style="width: min({width}px, calc(100vw - 32px)); --arrow: {Math.max(14, anchorWidth / 2 - 8)}px"
		>
			<div class="body">{@render children()}</div>
		</div>
	{/if}
</div>

<style>
	.anchor {
		position: relative;
		display: inline-flex;
	}
	.pop {
		position: absolute;
		top: calc(100% + 12px);
		z-index: 20;
		background: var(--panel);
		border: 1px solid var(--border);
		border-radius: 14px;
		box-shadow: 0 14px 36px rgb(0 0 0 / 0.55);
	}
	.body {
		display: flex;
		flex-direction: column;
		gap: 10px;
		padding: 16px;
		max-height: calc(100vh - 100px);
		overflow-y: auto;
	}
	.pop.left {
		left: 0;
	}
	.pop.right {
		right: 0;
	}
	/* The arrow: a rotated square sharing the panel's border, centred on the button */
	.pop::before {
		content: '';
		position: absolute;
		top: -8px;
		width: 14px;
		height: 14px;
		background: var(--panel);
		border-left: 1px solid var(--border);
		border-top: 1px solid var(--border);
		transform: rotate(45deg);
	}
	.pop.left::before {
		left: var(--arrow);
	}
	.pop.right::before {
		right: var(--arrow);
	}
</style>
