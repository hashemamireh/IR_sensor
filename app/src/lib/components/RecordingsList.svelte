<script lang="ts">
	import type { DeviceController } from '$lib/device.svelte';
	import { bytes, duration, localTime, rate, utcTime } from '$lib/format';
	import { displayName, matchesSearch, mergeRecordings, type RecordingRow } from '$lib/recordings';
	import Dialog from './Dialog.svelte';

	let {
		dev,
		selected = $bindable(),
		compare = $bindable(),
		maxTraces,
	}: { dev: DeviceController; selected: string | null; compare: string[]; maxTraces: number } = $props();

	const allRows = $derived(mergeRecordings(dev.online ? dev.ssid : null, dev.online ? dev.deviceRecordings : null, dev.locals));
	const onDevice = $derived(allRows.filter((r) => r.device));
	let query = $state('');
	const rows = $derived(allRows.filter((r) => matchesSearch(r, query)));

	/** Tap: show this recording alone */
	function open(key: string) {
		selected = key;
		compare = [];
	}

	/** Compare: add to / remove from the graph's extra traces */
	function toggleCompare(key: string) {
		if (key === selected) {
			// Removing the main trace promotes the next one
			selected = compare[0] ?? null;
			compare = compare.slice(1);
		} else if (compare.includes(key)) {
			compare = compare.filter((k) => k !== key);
		} else if (!selected) {
			selected = key;
		} else if (compare.length + 1 < maxTraces) {
			compare = [...compare, key];
		}
	}
	const inGraph = (key: string) => key === selected || compare.includes(key);
	const full = $derived((selected ? 1 : 0) + compare.length >= maxTraces);

	// ── Delete one ──────────────────────────────────────────────────────────
	let deleting = $state<RecordingRow | null>(null);
	let fromDevice = $state(false);
	let fromTablet = $state(false);
	let busy = $state(false);

	function askDelete(row: RecordingRow) {
		deleting = row;
		// Default: free the device, keep the tablet copy. If there's only one copy,
		// that copy is preselected (and the dialog warns).
		fromDevice = !!row.device;
		fromTablet = !row.device;
	}

	const lastCopy = $derived(
		!!deleting && (!deleting.device || fromDevice) && (!deleting.local || fromTablet),
	);

	async function confirmDelete() {
		if (!deleting) return;
		busy = true;
		const row = deleting;
		const ok = await dev.deleteRecording(row.key, {
			device: fromDevice ? row.device : undefined,
			local: fromTablet ? row.local : undefined,
		});
		busy = false;
		if (ok) {
			if (fromTablet && inGraph(row.key)) toggleCompare(row.key);
			deleting = null;
		}
	}

	// ── Delete all on device ────────────────────────────────────────────────
	let deletingAll = $state(false);
	const notOnTablet = $derived(onDevice.filter((r) => !r.local).length);

	async function confirmDeleteAll() {
		busy = true;
		await dev.deleteAllOnDevice();
		busy = false;
		deletingAll = false;
	}
</script>

<section>
	<div class="head">
		<h2>Recordings <span class="muted">{query ? `${rows.length} of ${allRows.length}` : allRows.length}</span></h2>
		<button class="danger small" disabled={!dev.idle || onDevice.length === 0} onclick={() => (deletingAll = true)}>
			Delete all on device
		</button>
	</div>

	<input class="search" type="search" placeholder="Search names, tags, notes, #id" bind:value={query} />

	{#if !dev.online}
		<p class="muted note">Device offline — showing recordings on this tablet.</p>
	{:else if dev.deviceRecordings === null}
		<p class="muted note">Reading the device's list…</p>
	{/if}

	<ul>
		{#each rows as row (row.key)}
			{@const failed = dev.failed[row.key]}
			<li class:selected={selected === row.key} class:compared={compare.includes(row.key)}>
				<button class="main" disabled={!row.local} onclick={() => open(row.key)}>
					<span class="line1">
						<strong class="title">{row.local ? displayName(row.local) : `#${row.id}`}</strong>
						{#if row.local?.name}<span class="muted">#{row.id}</span>{/if}
					</span>
					<span class="line2 muted">
						<span title={utcTime(row.start_utc_us)}>{localTime(row.start_utc_us)}</span> ·
						{duration(row.duration_ms)} · {rate(row.sample_rate_mhz)}
						{#if row.device}· {bytes(row.device.size_bytes)}{:else if row.local}· {bytes(row.local.size_bytes)}{/if}
						{#if row.ssid !== dev.ssid}· {row.ssid}{/if}
					</span>
					<span class="badges">
						{#if row.device}<span class="badge">device</span>{/if}
						{#if row.local}<span class="badge ok">tablet</span>{/if}
						{#if row.overrun}<span class="badge warn">overrun</span>{/if}
						{#if row.local && !row.local.crc_ok}<span class="badge danger">CRC failed</span>{/if}
						{#if dev.downloading === row.key}<span class="badge">downloading…</span>{/if}
						{#each row.local?.tags ?? [] as t (t)}<span class="badge tag">{t}</span>{/each}
					</span>
					{#if failed}<span class="err">{failed}</span>{/if}
				</button>
				<div class="actions">
					{#if row.local}
						<button
							class="small"
							class:on={inGraph(row.key)}
							disabled={!inGraph(row.key) && full}
							aria-pressed={inGraph(row.key)}
							title={full && !inGraph(row.key) ? `At most ${maxTraces} traces` : 'Show on the graph with the others'}
							onclick={() => toggleCompare(row.key)}>{inGraph(row.key) ? '✓ Compare' : '+ Compare'}</button
						>
					{/if}
					{#if row.device && !row.local && dev.downloading !== row.key && (failed || dev.isSkipped(row.key))}
						<button class="small" disabled={!dev.idle} onclick={() => dev.downloadNow(row.device!)}>
							{failed ? 'Retry' : 'Download'}
						</button>
					{/if}
					<button class="small danger" aria-label="Delete #{row.id}" onclick={() => askDelete(row)}>Delete</button>
				</div>
			</li>
		{:else}
			<li class="empty muted">{query ? 'Nothing matches.' : 'No recordings yet.'}</li>
		{/each}
	</ul>
</section>

{#if deleting}
	{@const row = deleting}
	<Dialog title="Delete recording #{row.id}?" onclose={() => (deleting = null)}>
		<p class="muted meta">{localTime(row.start_utc_us)} · {duration(row.duration_ms)} · {rate(row.sample_rate_mhz)}</p>
		<div class="choices">
			{#if row.device}
				<label class="check">
					<input type="checkbox" bind:checked={fromDevice} disabled={!dev.idle} />
					<span>From the device{#if !dev.idle}<span class="muted"> (device busy or offline)</span>{/if}</span>
				</label>
			{/if}
			{#if row.local}
				<label class="check">
					<input type="checkbox" bind:checked={fromTablet} />
					<span>From this tablet</span>
				</label>
			{/if}
		</div>
		{#if row.device && !row.local && fromDevice}
			<p class="banner danger">This recording hasn't been downloaded. Deleting it from the device loses it for good.</p>
		{:else if lastCopy && (fromDevice || fromTablet)}
			<p class="banner danger">This is the last copy. It can't be recovered.</p>
		{:else if fromTablet && row.device && !fromDevice}
			<p class="banner">The device keeps its copy. It won't be downloaded again automatically.</p>
		{/if}
		{#snippet actions()}
			<button onclick={() => (deleting = null)}>Cancel</button>
			<button
				class="danger solid"
				disabled={busy || (!fromDevice && !fromTablet) || (fromDevice && !dev.idle)}
				onclick={confirmDelete}>Delete</button
			>
		{/snippet}
	</Dialog>
{/if}

{#if deletingAll}
	<Dialog title="Delete all recordings on the device?" onclose={() => (deletingAll = false)}>
		<p>This deletes all {onDevice.length} recording{onDevice.length === 1 ? '' : 's'} stored on {dev.ssid}. Copies on this tablet are kept.</p>
		{#if notOnTablet > 0}
			<p class="banner danger">
				{notOnTablet} of them {notOnTablet === 1 ? "isn't" : "aren't"} on this tablet yet and will be lost for good.
			</p>
		{/if}
		{#snippet actions()}
			<button onclick={() => (deletingAll = false)}>Cancel</button>
			<button class="danger solid" disabled={busy || !dev.idle} onclick={confirmDeleteAll}>
				Delete {onDevice.length} from device
			</button>
		{/snippet}
	</Dialog>
{/if}

<style>
	section {
		display: flex;
		flex-direction: column;
		gap: 8px;
		min-height: 0;
		flex: 1;
	}
	.head {
		display: flex;
		align-items: center;
		justify-content: space-between;
		gap: 8px;
	}
	h2 {
		margin: 0;
		font-size: 16px;
	}
	.search {
		min-height: 40px;
		width: 100%;
	}
	li.compared {
		border-color: color-mix(in srgb, var(--accent) 50%, transparent);
	}
	.small.on {
		border-color: var(--accent);
		background: color-mix(in srgb, var(--accent) 18%, var(--panel-2));
	}
	.badge.tag {
		color: var(--accent);
		border-color: color-mix(in srgb, var(--accent) 40%, transparent);
	}
	.small {
		min-height: 38px;
		padding: 0 12px;
		font-size: 14px;
	}
	.note {
		margin: 0;
		font-size: 13px;
	}
	ul {
		list-style: none;
		margin: 0;
		padding: 0;
		overflow-y: auto;
		display: flex;
		flex-direction: column;
		gap: 6px;
		min-height: 0;
		flex: 1;
		touch-action: pan-y;
	}
	li {
		display: flex;
		align-items: stretch;
		border: 1px solid var(--border);
		border-radius: var(--radius);
		background: var(--panel-2);
	}
	li.selected {
		border-color: var(--accent);
		background: color-mix(in srgb, var(--accent) 10%, var(--panel-2));
	}
	li.empty {
		padding: 16px;
		justify-content: center;
	}
	.main {
		flex: 1;
		min-width: 0;
		border: none;
		background: none;
		text-align: left;
		padding: 8px 12px;
		display: flex;
		flex-direction: column;
		gap: 3px;
	}
	.main:disabled {
		opacity: 1;
	}
	.line1 {
		display: flex;
		gap: 8px;
		align-items: baseline;
		min-width: 0;
	}
	.title {
		overflow: hidden;
		text-overflow: ellipsis;
		white-space: nowrap;
	}
	.line2 {
		font-size: 13px;
	}
	.badges {
		display: flex;
		gap: 4px;
		flex-wrap: wrap;
	}
	.err {
		color: var(--danger);
		font-size: 12px;
	}
	.actions {
		display: flex;
		flex-direction: column;
		justify-content: center;
		gap: 6px;
		padding: 8px;
	}
	.meta {
		margin: 0;
	}
	.choices {
		display: flex;
		flex-direction: column;
		gap: 6px;
	}
	.check {
		display: flex;
		align-items: center;
		gap: 12px;
		min-height: var(--tap);
		padding: 0 12px;
		border: 1px solid var(--border);
		border-radius: var(--radius);
	}
	.check input {
		width: 22px;
		height: 22px;
		min-height: 0;
		accent-color: var(--danger);
	}
	p {
		margin: 0;
	}
</style>
