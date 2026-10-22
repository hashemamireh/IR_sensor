<script lang="ts">
	import { DEVICE_URL, MOCK_URL, type DeviceController } from '$lib/device.svelte';
	import { bytes } from '$lib/format';

	let { dev }: { dev: DeviceController } = $props();

	let open = $state(false);
	let custom = $state('');

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

	<div class="target">
		<button onclick={() => (open = !open)} aria-expanded={open}>Device: {host}</button>
		{#if open}
			<div class="menu">
				<button class:on={dev.baseUrl === DEVICE_URL} onclick={() => choose(DEVICE_URL)}>
					IR sensor <span class="muted">{DEVICE_URL}</span>
				</button>
				<button class:on={dev.baseUrl === MOCK_URL} onclick={() => choose(MOCK_URL)}>
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
			</div>
		{/if}
	</div>
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
	.target {
		position: relative;
	}
	.menu {
		position: absolute;
		right: 0;
		top: calc(100% + 6px);
		z-index: 10;
		width: 340px;
		display: flex;
		flex-direction: column;
		gap: 8px;
		padding: 12px;
		background: var(--panel);
		border: 1px solid var(--border);
		border-radius: 12px;
		box-shadow: 0 12px 30px rgb(0 0 0 / 0.5);
	}
	.menu > button {
		text-align: left;
		display: flex;
		justify-content: space-between;
	}
	.menu > button.on {
		border-color: var(--accent);
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
		margin: 4px 0 0;
		font-size: 12px;
		word-break: break-all;
	}
</style>
