<!-- Single-select chips (a segmented control) -->
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
		label,
	}: { options: Option[]; value: T; disabled?: boolean; label: string } = $props();
</script>

<div class="seg" role="radiogroup" aria-label={label}>
	{#each options as o (o.label)}
		<button
			type="button"
			role="radio"
			aria-checked={o.value === value}
			class:on={o.value === value}
			disabled={disabled || o.disabled}
			onclick={() => (value = o.value)}>{o.label}</button
		>
	{/each}
</div>

<style>
	.seg {
		display: flex;
		flex-wrap: wrap;
		gap: 6px;
	}
	button {
		flex: 1 0 auto;
		padding: 0 12px;
		min-width: 56px;
	}
	button.on {
		background: color-mix(in srgb, var(--accent) 22%, var(--panel-2));
		border-color: var(--accent);
		color: var(--text);
		font-weight: 600;
	}
</style>
