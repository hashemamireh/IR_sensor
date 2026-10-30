<script lang="ts">
	import { DEVICE_URL, MOCK_URL, type DeviceController } from '$lib/device.svelte';
	import { bytes } from '$lib/format';
	import { screen } from '$lib/layout.svelte';
	import Popover from './Popover.svelte';
	import RecordPanel from './RecordPanel.svelte';
	import Segmented from './Segmented.svelte';
	import SettingsPanel from './SettingsPanel.svelte';

	let { dev, liveOpen = $bindable() }: { dev: DeviceController; liveOpen: boolean } = $props();

	async function toggleLive() {
		if (dev.live) {
			liveOpen = !liveOpen; // already running: show / hide the view
		} else {
			liveOpen = true;
			if (!(await dev.startLive())) liveOpen = false; // refused; the notice says why
		}
	}

	let open = $state(false);
	let custom = $state('');
	let recordOpen = $state(false);
	/** The Device popover's tab: the device's settings, or which device to connect to */
	let deviceTab = $state<'settings' | 'connection'>('settings');

	function toggleDevice() {
		if (!open) deviceTab = dev.online ? 'settings' : 'connection';
		open = !open;
	}

	const rec = $derived(dev.online && dev.status?.state === 'recording' ? dev.status.recording : undefined);
	const saving = $derived(dev.online && dev.status?.state === 'saving');

	const host = $derived(dev.baseUrl.replace(/^https?:\/\//, ''));
	// Compact (portrait, phone): the SSID, free space, voltage and clock note move
	// into the Device menu; the bar keeps the dot, battery %, state and buttons
	const compact = $derived(screen.compact);
	const label = $derived(
		dev.connection === 'online'
			? compact
				? ''
				: dev.ssid
			: dev.connection === 'connecting'
				? 'Connecting…'
				: compact
					? 'Offline'
					: 'Device not reachable',
	);
	const stateText: Record<string, string> = {
		idle: 'Ready',
		recording: 'Recording',
		saving: 'Saving',
		compressing: 'Compressing',
		live: 'Live',
	};

	function choose(url: string) {
		dev.setBaseUrl(url);
		open = false;
	}
</script>

<header class:compact class:phone={screen.phone}>
	{#if !screen.phone}<strong class="title">IR Sensor</strong>{/if}

	<!-- Status text; the buttons sit to its right and never wrap -->
	<div class="info">
		<span class="conn {dev.connection}" title={dev.online ? dev.ssid : undefined}>
			<span class="dot"></span>{label}
		</span>

		{#if dev.online && dev.status}
			{@const s = dev.status}
			{#if compact}
				<span class="item batt" class:low={s.battery.pct <= 15} title="Battery {(s.battery.mv / 1000).toFixed(2)} V">
					<svg viewBox="0 0 24 12" aria-label="Battery">
						<rect x="0.75" y="0.75" width="19.5" height="10.5" rx="2" fill="none" stroke="currentColor" stroke-width="1.5" />
						<rect x="21" y="3.5" width="2.5" height="5" rx="1" fill="currentColor" />
						<rect x="2.5" y="2.5" width={Math.max(1, (16 * Math.min(100, s.battery.pct)) / 100)} height="7" rx="1" fill="currentColor" />
					</svg>
					{s.battery.pct}%
				</span>
			{:else}
				<span class="item" class:low={s.battery.pct <= 15} title="Battery {s.battery.mv} mV">
					Battery {s.battery.pct}% · {(s.battery.mv / 1000).toFixed(2)} V
				</span>
				<span class="item">{bytes(s.free_bytes)} free</span>
			{/if}
			<!-- On phones the Record button already shows recording / saving progress -->
			{#if !(screen.phone && (rec || saving))}
				<span class="badge {s.state === 'idle' || s.state === 'live' ? 'ok' : 'warn'}">{stateText[s.state] ?? s.state}</span>
			{/if}
			{#if !dev.timeSynced && !compact}<span class="badge">clock not synced yet</span>{/if}
		{/if}
	</div>

	<div class="actions">
		<button
			class="livebtn"
			class:on={dev.live}
			disabled={!dev.ready}
			aria-pressed={liveOpen}
			title={dev.live ? (liveOpen ? 'Hide live view' : 'Show live view') : 'Start live view'}
			aria-label="Live"
			onclick={toggleLive}
		>
			<span class="ldot"></span>{#if !screen.phone}Live{/if}
		</button>

		<Popover open={recordOpen} onclose={() => (recordOpen = false)} align="right" width={540} label="Record">
			{#snippet trigger()}
				<button
					class="rec"
					class:live={rec || saving}
					disabled={!dev.online}
					aria-expanded={recordOpen}
					aria-label="Record"
					onclick={() => (recordOpen = !recordOpen)}
				>
					<span class="recdot"></span>
					{#if rec}
						<span class="num">{rec.elapsed_s} / {rec.total_s} s</span>
					{:else if saving}
						Saving…
					{:else if !screen.phone}
						Record
					{/if}
				</button>
			{/snippet}
			<RecordPanel {dev} />
		</Popover>

		<Popover open={open} onclose={() => (open = false)} align="right" width={460} label="Device">
			{#snippet trigger()}
				{#if compact}
					<button class="gear" onclick={toggleDevice} aria-expanded={open} aria-label="Device" title="Device: {host}">
						<svg viewBox="0 0 24 24" aria-hidden="true">
							<path
								fill="none"
								stroke="currentColor"
								stroke-width="1.8"
								stroke-linejoin="round"
								d="M10.3 2.5h3.4l.5 2.6a7.5 7.5 0 0 1 1.9 1.1l2.5-.9 1.7 2.9-2 1.7a7.6 7.6 0 0 1 0 2.2l2 1.7-1.7 2.9-2.5-.9a7.5 7.5 0 0 1-1.9 1.1l-.5 2.6h-3.4l-.5-2.6a7.5 7.5 0 0 1-1.9-1.1l-2.5.9-1.7-2.9 2-1.7a7.6 7.6 0 0 1 0-2.2l-2-1.7 1.7-2.9 2.5.9a7.5 7.5 0 0 1 1.9-1.1z"
							/>
							<circle cx="12" cy="12" r="3" fill="none" stroke="currentColor" stroke-width="1.8" />
						</svg>
					</button>
				{:else}
					<button onclick={toggleDevice} aria-expanded={open}>Device: {host}</button>
				{/if}
			{/snippet}
			{#if compact}
				<!-- What the compact bar leaves out -->
				<div class="about">
					<span><strong>{dev.online ? dev.ssid : label}</strong> <span class="muted">· {host}</span></span>
					{#if dev.online && dev.status}
						{@const s = dev.status}
						<span class="muted">
							Battery {s.battery.pct}% · {(s.battery.mv / 1000).toFixed(2)} V · {bytes(s.free_bytes)} free
						</span>
						{#if !dev.timeSynced}<span><span class="badge">clock not synced yet</span></span>{/if}
					{/if}
				</div>
			{/if}
			<Segmented label="Device menu" bind:value={deviceTab} options={[
				{ value: 'settings', label: 'Settings' },
				{ value: 'connection', label: 'Connection' },
			]} />

			{#if deviceTab === 'settings'}
				{#if dev.online}
					<SettingsPanel {dev} title={false} onclose={() => (open = false)} />
				{:else}
					<p class="muted">Connect to a device to change its settings.</p>
				{/if}
			{:else}
				<button class="choice" class:on={dev.baseUrl === DEVICE_URL} onclick={() => choose(DEVICE_URL)}>
					IR sensor <span class="muted">{DEVICE_URL}</span>
				</button>
				<button class="choice" class:on={dev.baseUrl === MOCK_URL} onclick={() => choose(MOCK_URL)}>
					Mock device <span class="muted">{MOCK_URL}</span>
				</button>
				<form
					onsubmit={(e) => {
						e.preventDefault();
						if (custom.trim()) choose(custom.includes('://') ? custom : `http://${custom}`);
					}}
				>
					<input placeholder="Other, e.g. 192.168.1.20:8080" bind:value={custom} />
					<button type="submit">Use</button>
				</form>
				<p class="muted path">Recordings are saved in<br /><code>{dev.local.root}/recordings</code></p>
			{/if}
		</Popover>
	</div>
</header>


<style>
	header {
		display: flex;
		align-items: center;
		gap: 14px;
		padding: 8px 16px;
		background: var(--panel);
		border-bottom: 1px solid var(--border);
		min-height: 60px;
	}
	header.phone {
		gap: 10px;
		padding: 6px 10px;
	}
	.title {
		flex: none;
		font-size: 17px;
	}
	.info {
		flex: 1 1 auto;
		min-width: 0;
		display: flex;
		align-items: center;
		gap: 14px;
		white-space: nowrap;
		overflow: hidden;
	}
	.info > * {
		flex: none;
	}
	.actions {
		flex: none;
		display: flex;
		align-items: center;
		gap: 8px;
	}
	.conn {
		display: inline-flex;
		align-items: center;
		gap: 8px;
		font-weight: 600;
	}
	.dot {
		width: 10px;
		height: 10px;
		border-radius: 50%;
		background: var(--muted);
	}
	.online .dot {
		background: var(--ok);
	}
	.unreachable .dot {
		background: var(--danger);
	}
	.connecting .dot {
		background: var(--warn);
	}
	.item {
		color: var(--muted);
		font-variant-numeric: tabular-nums;
	}
	.item.low {
		color: var(--warn);
	}
	.batt {
		display: inline-flex;
		align-items: center;
		gap: 6px;
		color: var(--text);
	}
	.batt svg {
		width: 22px;
		height: 11px;
	}
	.gear {
		padding: 0;
		width: var(--tap);
		display: inline-flex;
		align-items: center;
		justify-content: center;
	}
	.gear svg {
		width: 22px;
		height: 22px;
	}
	/* Icon-only on phones: square buttons */
	.phone .livebtn,
	.phone .rec {
		padding: 0 14px;
		justify-content: center;
		min-width: var(--tap);
	}
	.about {
		display: flex;
		flex-direction: column;
		gap: 4px;
		padding-bottom: 10px;
		border-bottom: 1px solid var(--border);
		font-size: 14px;
	}
	.choice {
		flex: none;
		text-align: left;
		display: flex;
		align-items: center;
		justify-content: space-between;
	}
	.choice.on {
		border-color: var(--accent);
	}
	/* Green, as the record button is red; brighter with a pulsing dot while live */
	.livebtn {
		display: inline-flex;
		align-items: center;
		gap: 10px;
		font-weight: 600;
		background: color-mix(in srgb, var(--ok) 18%, var(--panel-2));
		border-color: color-mix(in srgb, var(--ok) 60%, transparent);
	}
	.ldot {
		width: 11px;
		height: 11px;
		border-radius: 50%;
		background: var(--ok);
		flex: none;
	}
	.livebtn.on {
		background: color-mix(in srgb, var(--ok) 28%, var(--panel-2));
		border-color: var(--ok);
	}
	.livebtn.on .ldot {
		animation: pulse 1.2s ease-in-out infinite;
	}
	/* Same look as the record button in the panel */
	.rec {
		display: inline-flex;
		align-items: center;
		gap: 10px;
		font-weight: 600;
		background: color-mix(in srgb, var(--rec) 18%, var(--panel-2));
		border-color: color-mix(in srgb, var(--rec) 60%, transparent);
	}
	.recdot {
		width: 11px;
		height: 11px;
		border-radius: 50%;
		background: var(--rec);
		flex: none;
	}
	.rec.live .recdot {
		animation: pulse 1.2s ease-in-out infinite;
	}
	.num {
		font-variant-numeric: tabular-nums;
	}
	@keyframes pulse {
		50% {
			opacity: 0.25;
		}
	}
	form {
		display: flex;
		gap: 8px;
	}
	form input {
		flex: 1;
		min-width: 0;
	}
	.path {
		margin: 0;
		font-size: 12px;
		word-break: break-all;
	}
</style>
