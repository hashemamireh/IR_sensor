<script lang="ts">
	import { DEVICE_URL, MOCK_URL, type DeviceController } from '$lib/device.svelte';
	import { bytes } from '$lib/format';
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
	const label = $derived(
		dev.connection === 'online'
			? dev.ssid
			: dev.connection === 'connecting'
				? 'Connecting…'
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

<header>
	<strong class="title">IR Sensor</strong>

	<!-- Status text; the buttons sit to its right and never wrap -->
	<div class="info">
		<span class="conn {dev.connection}">
			<span class="dot"></span>{label}
		</span>

		{#if dev.online && dev.status}
			{@const s = dev.status}
			<span class="item" class:low={s.battery.pct <= 15} title="Battery {s.battery.mv} mV">
				Battery {s.battery.pct}% · {(s.battery.mv / 1000).toFixed(2)} V
			</span>
			<span class="item">{bytes(s.free_bytes)} free</span>
			<span class="badge {s.state === 'idle' || s.state === 'live' ? 'ok' : 'warn'}">{stateText[s.state] ?? s.state}</span>
			{#if !dev.timeSynced}<span class="badge">clock not synced yet</span>{/if}
		{/if}
	</div>

	<div class="actions">
		<button
			class="livebtn"
			class:on={dev.live}
			disabled={!dev.ready}
			aria-pressed={liveOpen}
			title={dev.live ? (liveOpen ? 'Hide live view' : 'Show live view') : 'Start live view'}
			onclick={toggleLive}
		>
			<span class="ldot"></span> Live
		</button>

		<Popover open={recordOpen} onclose={() => (recordOpen = false)} align="right" width={540} label="Record">
			{#snippet trigger()}
				<button
					class="rec"
					class:live={rec || saving}
					disabled={!dev.online}
					aria-expanded={recordOpen}
					onclick={() => (recordOpen = !recordOpen)}
				>
					<span class="recdot"></span>
					{#if rec}
						<span class="num">{rec.elapsed_s} / {rec.total_s} s</span>
					{:else if saving}
						Saving…
					{:else}
						Record
					{/if}
				</button>
			{/snippet}
			<RecordPanel {dev} />
		</Popover>

		<Popover open={open} onclose={() => (open = false)} align="right" width={460} label="Device">
			{#snippet trigger()}
				<button onclick={toggleDevice} aria-expanded={open}>Device: {host}</button>
			{/snippet}
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
