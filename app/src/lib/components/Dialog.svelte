<!-- Modal confirm dialog. Closes on Escape / backdrop click via `onclose`. -->
<script lang="ts">
	import type { Snippet } from 'svelte';
	let {
		title,
		onclose,
		children,
		actions,
	}: { title: string; onclose: () => void; children: Snippet; actions: Snippet } = $props();

	let dialog: HTMLDialogElement;
	$effect(() => {
		dialog.showModal();
		return () => dialog.close();
	});
</script>

<dialog
	bind:this={dialog}
	oncancel={(e) => {
		e.preventDefault();
		onclose();
	}}
	onclick={(e) => {
		if (e.target === dialog) onclose();
	}}
>
	<div class="body">
		<h2>{title}</h2>
		{@render children()}
		<div class="actions">{@render actions()}</div>
	</div>
</dialog>

<style>
	dialog {
		padding: 0;
		border: 1px solid var(--border);
		border-radius: 14px;
		background: var(--panel);
		color: var(--text);
		width: min(480px, calc(100vw - 32px));
	}
	dialog::backdrop {
		background: rgb(0 0 0 / 0.6);
	}
	.body {
		padding: 20px 22px;
		display: flex;
		flex-direction: column;
		gap: 14px;
	}
	h2 {
		margin: 0;
		font-size: 18px;
	}
	.actions {
		display: flex;
		justify-content: flex-end;
		gap: 10px;
		margin-top: 4px;
	}
</style>
