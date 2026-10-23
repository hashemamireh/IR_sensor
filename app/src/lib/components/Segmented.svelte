<!-- Single-select segmented control: one joined bar of buttons, the chosen one highlighted.
     `fill` stretches it to its container with equal segments; off, it sizes to its labels. -->
<script lang="ts" generics="T">
	interface Option {
		value: T;
		label: string;
		disabled?: boolean;
	}
	let {
		options,
		value = $bindable(),
		disabled = false,
		fill = true,
		label,
	}: { options: Option[]; value: T; disabled?: boolean; fill?: boolean; label: string } = $props();
</script>

<div class="seg" class:fill role="radiogroup" aria-label={label}>
	{#each options as o (o.label)}
		<button
			type="button"
			role="radio"
			aria-checked={o.value === value}
			class:on={o.value === value}
			disabled={disabled || o.disabled}
			title={o.label}
			onclick={() => (value = o.value)}>{o.label}</button
		>
	{/each}
</div>

<style>
	.seg {
		display: flex;
	}
	button {
		position: relative;
		flex: none;
		min-width: 0;
		padding: 0 12px;
		border-radius: 0;
		margin-left: -1px; /* neighbours share one border */
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
	}
	.fill button {
		flex: 1 1 0;
		padding: 0 6px;
	}
	button:first-child {
		margin-left: 0;
		border-radius: var(--radius) 0 0 var(--radius);
	}
	button:last-child {
		border-radius: 0 var(--radius) var(--radius) 0;
	}
	button:only-child {
		border-radius: var(--radius);
	}
	/* Raise the hovered / chosen segment so its whole border shows */
	button:hover:not(:disabled) {
		z-index: 1;
	}
	button.on {
		z-index: 2;
		background: color-mix(in srgb, var(--accent) 22%, var(--panel-2));
		border-color: var(--accent);
		font-weight: 600;
	}
</style>
