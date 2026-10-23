<!-- Export one recording as CSV or JSON: whole or visible window, full or lower rate -->
<script lang="ts">
	import { downsampleChoices, estimateBytes, exportChunks, rowCount, type ExportFormat, type ExportOptions } from '$lib/export';
	import { chooseExportPath, writeExport } from '$lib/exportFile';
	import { bytes, errorText, rate as fmtRate } from '$lib/format';
	import type { IrecFile } from '$lib/irec';
	import { baseName, displayName, type LocalRecording } from '$lib/recordings';
	import Dialog from './Dialog.svelte';
	import Segmented from './Segmented.svelte';

	let {
		recording,
		file,
		lut,
		window: view,
		dataDir,
		onclose,
	}: {
		recording: LocalRecording;
		file: IrecFile;
		lut: Float32Array | null;
		/** Visible window in seconds, or null if the whole recording is shown */
		window: [number, number] | null;
		dataDir: string;
		onclose: () => void;
	} = $props();

	const rateHz = $derived(file.header.sampleRateMhz / 1000);
	const n = $derived(file.samples.length);

	let format = $state<ExportFormat>('csv');
	let range = $state<'all' | 'window'>('all');
	let factor = $state(1);

	const choices = $derived(downsampleChoices(rateHz));
	const [i0, i1] = $derived.by(() => {
		if (range === 'window' && view) {
			return [Math.max(0, Math.ceil(view[0] * rateHz)), Math.min(n, Math.floor(view[1] * rateHz) + 1)];
		}
		return [0, n];
	});
	const options = $derived<ExportOptions>({ format, i0, i1, factor });
	const source = $derived({
		samples: file.samples,
		rateHz,
		lut,
		meta: {
			id: recording.id,
			device_ssid: recording.device_ssid,
			name: recording.name,
			tags: recording.tags,
			notes: recording.notes,
			start_utc_us: recording.start_utc_us,
			sample_rate_mhz: file.header.sampleRateMhz,
			sample_count: file.header.sampleCount,
			attenuation_db: file.header.attenTenthsDb / 10,
			overrun: file.header.overrun,
			crc_ok: file.crcOk,
			source_file: recording.file,
		},
	});
	const rows = $derived(rowCount(options));
	const estimate = $derived(estimateBytes(source, options));

	function fileName() {
		const stem = recording.name?.trim()
			? recording.name.trim().replace(/[^\p{L}\p{N}_-]+/gu, '_')
			: baseName(recording.device_ssid, recording.id);
		const win = range === 'window' ? `_${(i0 / rateHz).toFixed(3)}-${(i1 / rateHz).toFixed(3)}s` : '';
		const res = factor > 1 ? `_${choices.find((c) => c.factor === factor)?.label.replace(' ', '')}` : '';
		return `${stem}${win}${res}.${format}`;
	}

	let progress = $state<number | null>(null);
	let cancel = false;
	let result = $state<{ ok: boolean; text: string } | null>(null);

	async function run() {
		result = null;
		try {
			const path = await chooseExportPath(dataDir, fileName(), format);
			if (!path) return;
			cancel = false;
			progress = 0;
			const ok = await writeExport(path, exportChunks(source, options), (p) => (progress = p), () => cancel);
			result = ok ? { ok: true, text: `Saved ${path}` } : { ok: false, text: 'Export cancelled.' };
		} catch (e) {
			result = { ok: false, text: `Export failed: ${errorText(e)}` };
		} finally {
			progress = null;
		}
	}
</script>

<Dialog title="Export {displayName(recording)}" onclose={() => (progress === null ? onclose() : (cancel = true))}>
	<span class="label">Format</span>
	<Segmented label="Format" bind:value={format} disabled={progress !== null} options={[
		{ value: 'csv', label: 'CSV' },
		{ value: 'json', label: 'JSON' },
	]} />

	<span class="label">Range</span>
	<Segmented label="Range" bind:value={range} disabled={progress !== null} options={[
		{ value: 'all', label: 'Whole recording' },
		{ value: 'window', label: 'Visible window', disabled: !view },
	]} />

	<span class="label">Resolution</span>
	<Segmented label="Resolution" bind:value={factor} disabled={progress !== null} options={[
		{ value: 1, label: `Full (${fmtRate(file.header.sampleRateMhz)})` },
		...choices.map((c) => ({ value: c.factor, label: c.label })),
	]} />

	<p class="muted small">
		{rows.toLocaleString()} rows · about {bytes(estimate)}
		{#if factor > 1}
			<br />Each row averages {factor} samples, so times are only good to about {+((1000 * factor) / rateHz).toPrecision(2)} ms.
		{/if}
		{#if !lut}<br />No calibration table: <code>voltage_mv</code> will be empty.{/if}
		{#if file.header.overrun}<br />This recording has an overrun: its timeline is unreliable.{/if}
	</p>

	{#if progress !== null}
		<div class="bar"><div style="width: {Math.round(progress * 100)}%"></div></div>
	{/if}
	{#if result}
		<p class="banner {result.ok ? '' : 'danger'} small path">{result.text}</p>
	{/if}

	{#snippet actions()}
		{#if progress !== null}
			<button onclick={() => (cancel = true)}>Cancel export</button>
		{:else}
			<button onclick={onclose}>{result?.ok ? 'Done' : 'Close'}</button>
			<button class="primary" disabled={rows === 0} onclick={run}>Export…</button>
		{/if}
	{/snippet}
</Dialog>

<style>
	.label {
		font-size: 13px;
		color: var(--muted);
		margin-bottom: -8px;
	}
	.small {
		font-size: 13px;
		margin: 0;
	}
	.path {
		word-break: break-all;
	}
	.bar {
		height: 10px;
		border-radius: 5px;
		background: var(--bg);
		overflow: hidden;
	}
	.bar > div {
		height: 100%;
		background: var(--accent);
	}
</style>
