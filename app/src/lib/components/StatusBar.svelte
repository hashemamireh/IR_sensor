<script lang="ts">
	import { DEVICE_URL, MOCK_URL, type DeviceController } from '$lib/device.svelte';
	import { bytes } from '$lib/format';
	import Popover from './Popover.svelte';
	import RecordPanel from './RecordPanel.svelte';
	import SettingsPanel from './SettingsPanel.svelte';

	let { dev }: { dev: DeviceController } = $props();

	let open = $state(false);
	let custom = $state('');
	let settingsOpen = $state(false);
	let recordOpen = $state(false);

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
	};

	function choose(url: string) {
		dev.setBaseUrl(url);
		open = false;
	}
</script>

<header>
	<strong class="title">IR Sensor</strong>

	<span class="conn {dev.connection}">
		<span class="dot"></span>{label}
	</span>

	{#if dev.online && dev.status}
		{@const s = dev.status}
		<span class="item" class:low={s.battery.pct <= 15} title="{s.battery.mv} mV">
			Battery {s.battery.pct}% · {(s.battery.mv / 1000).toFixed(2)} V
		</span>
		<span class="item">{bytes(s.free_bytes)} free</span>
		<span class="badge {s.state === 'idle' ? 'ok' : 'warn'}">{stateText[s.state] ?? s.state}</span>
		{#if !dev.timeSynced}<span class="badge">clock not synced yet</span>{/if}
	{/if}

	<span class="spacer"></span>

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

	<Popover open={settingsOpen} onclose={() => (settingsOpen = false)} align="right" width={440} label="Device Settings">
		{#snippet trigger()}
			<button disabled={!dev.online} aria-expanded={settingsOpen} onclick={() => (settingsOpen = !settingsOpen)}>
				Device Settings
			</button>
		{/snippet}
		<SettingsPanel {dev} onclose={() => (settingsOpen = false)} />
	</Popover>

	<Popover open={open} onclose={() => (open = false)} align="right" width={340} label="Device">
		{#snippet trigger()}
			<button onclick={() => (open = !open)} aria-expanded={open}>Device: {host}</button>
		{/snippet}
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
	</Popover>
</header>


<style>
	header {
		display: flex;
		align-items: center;
		gap: 16px;
		padding: 8px 16px;
		background: var(--panel);
		border-bottom: 1px solid var(--border);
		min-height: 60px;
		flex-wrap: wrap;
	}
	.title {
		font-size: 17px;
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
	.spacer {
		flex: 1;
	}
	.choice {
		text-align: left;
		display: flex;
		justify-content: space-between;
	}
	.choice.on {
		border-color: var(--accent);
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
