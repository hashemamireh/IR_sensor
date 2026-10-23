<!-- Name, tags and notes of one tablet recording (decision E5: tablet-only, in the sidecar) -->
<script lang="ts">
	import type { LocalRecording } from '$lib/recordings';

	type Patch = Partial<Pick<LocalRecording, 'name' | 'notes' | 'tags'>>;
	let {
		recording,
		suggestions,
		onsave,
	}: { recording: LocalRecording; suggestions: string[]; onsave: (patch: Patch) => void } = $props();

	// Drafts follow the recording, but not while you're typing in them
	let name = $state('');
	let notes = $state('');
	let tag = $state('');
	let editingName = false;
	let editingNotes = false;
	$effect(() => {
		const r = recording;
		if (!editingName) name = r.name ?? '';
		if (!editingNotes) notes = r.notes ?? '';
	});

	function saveName() {
		editingName = false;
		const v = name.trim() || null;
		if (v !== (recording.name ?? null)) onsave({ name: v });
	}

	function saveNotes() {
		editingNotes = false;
		if (notes !== (recording.notes ?? '')) onsave({ notes });
	}

	function addTag() {
		const t = tag.trim().replace(/,+$/, '');
		tag = '';
		if (t && !recording.tags.includes(t)) onsave({ tags: [...recording.tags, t] });
	}

	const listId = `tags-${Math.random().toString(36).slice(2)}`;
</script>

<div class="meta">
	<input
		class="name"
		placeholder="#{recording.id} — add a name"
		bind:value={name}
		onfocus={() => (editingName = true)}
		onblur={saveName}
		onkeydown={(e) => e.key === 'Enter' && (e.currentTarget as HTMLInputElement).blur()}
		aria-label="Name"
	/>
	<div class="tags">
		{#each recording.tags as t (t)}
			<span class="tag">
				{t}
				<button aria-label="Remove tag {t}" onclick={() => onsave({ tags: recording.tags.filter((x) => x !== t) })}>✕</button>
			</span>
		{/each}
		<input
			class="add"
			placeholder="+ tag"
			list={listId}
			bind:value={tag}
			onkeydown={(e) => {
				if (e.key === 'Enter' || e.key === ',') {
					e.preventDefault();
					addTag();
				}
			}}
			onchange={addTag}
			aria-label="Add tag"
		/>
		<datalist id={listId}>
			{#each suggestions.filter((s) => !recording.tags.includes(s)) as s (s)}<option value={s}></option>{/each}
		</datalist>
	</div>
	<details open={!!recording.notes}>
		<summary>Notes{recording.notes ? '' : ' (none)'}</summary>
		<textarea
			rows="3"
			placeholder="Setup, conditions, what you were measuring…"
			bind:value={notes}
			onfocus={() => (editingNotes = true)}
			onblur={saveNotes}
		></textarea>
	</details>
</div>

<style>
	.meta {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}
	.name {
		font-size: 16px;
		font-weight: 600;
		background: transparent;
		border-color: transparent;
		padding: 0 8px;
		margin-left: -8px;
		min-height: 38px;
	}
	.name:hover,
	.name:focus {
		border-color: var(--border);
		background: var(--bg);
	}
	.tags {
		display: flex;
		flex-wrap: wrap;
		gap: 6px;
		align-items: center;
	}
	.tag {
		display: inline-flex;
		align-items: center;
		gap: 2px;
		padding-left: 10px;
		border-radius: 999px;
		border: 1px solid color-mix(in srgb, var(--accent) 50%, transparent);
		background: color-mix(in srgb, var(--accent) 12%, var(--panel));
		font-size: 13px;
	}
	.tag button {
		min-height: 30px;
		padding: 0 10px;
		border: none;
		background: none;
		color: var(--muted);
	}
	.add {
		min-height: 32px;
		width: 120px;
		font-size: 13px;
		border-radius: 999px;
	}
	summary {
		cursor: pointer;
		color: var(--muted);
		font-size: 13px;
		min-height: 28px;
	}
	textarea {
		width: 100%;
		margin-top: 4px;
		font: inherit;
		color: inherit;
		background: var(--bg);
		border: 1px solid var(--border);
		border-radius: var(--radius);
		padding: 8px 12px;
		resize: vertical;
		-webkit-user-select: text;
		user-select: text;
	}
</style>
