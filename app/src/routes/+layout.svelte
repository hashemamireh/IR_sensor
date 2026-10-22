<script lang="ts">
	let { children } = $props();
</script>

{@render children()}

<style>
	/* Dark by default (spec §10.3): IR work happens in controlled lighting */
	:global(:root) {
		--bg: #0e1014;
		--panel: #161920;
		--panel-2: #1d212a;
		--border: #2a303c;
		--text: #e7e9ee;
		--muted: #97a0b0;
		--accent: #5aa9ff;
		--accent-ink: #06121f;
		--ok: #3ecf8e;
		--warn: #f2b84b;
		--danger: #ff6161;
		--rec: #ff4747;
		--radius: 10px;
		--tap: 44px;
		color-scheme: dark;
	}
	:global(html),
	:global(body) {
		margin: 0;
		height: 100%;
		background: var(--bg);
		color: var(--text);
		font: 15px/1.4 system-ui, -apple-system, 'Segoe UI', Cantarell, Ubuntu, sans-serif;
		/* Pan/scroll yes, pinch-zoom of the page no */
		touch-action: pan-x pan-y;
		-webkit-user-select: none;
		user-select: none;
		overflow: hidden;
	}
	:global(*),
	:global(*::before),
	:global(*::after) {
		box-sizing: border-box;
	}
	:global(button) {
		font: inherit;
		color: inherit;
		min-height: var(--tap);
		padding: 0 16px;
		border-radius: var(--radius);
		border: 1px solid var(--border);
		background: var(--panel-2);
		cursor: pointer;
	}
	:global(button:hover:not(:disabled)) {
		border-color: var(--muted);
	}
	:global(button:disabled) {
		opacity: 0.45;
		cursor: default;
	}
	:global(button.primary) {
		background: var(--accent);
		color: var(--accent-ink);
		border-color: var(--accent);
		font-weight: 600;
	}
	:global(button.danger) {
		background: transparent;
		color: var(--danger);
		border-color: color-mix(in srgb, var(--danger) 50%, transparent);
	}
	:global(button.danger.solid) {
		background: var(--danger);
		color: #1a0505;
		border-color: var(--danger);
		font-weight: 600;
	}
	:global(input) {
		font: inherit;
		color: inherit;
		background: var(--bg);
		border: 1px solid var(--border);
		border-radius: var(--radius);
		min-height: var(--tap);
		padding: 0 12px;
		-webkit-user-select: text;
		user-select: text;
	}
	:global(.badge) {
		display: inline-block;
		padding: 1px 8px;
		border-radius: 999px;
		font-size: 12px;
		border: 1px solid var(--border);
		color: var(--muted);
		white-space: nowrap;
	}
	:global(.badge.warn) {
		color: var(--warn);
		border-color: color-mix(in srgb, var(--warn) 45%, transparent);
	}
	:global(.badge.danger) {
		color: var(--danger);
		border-color: color-mix(in srgb, var(--danger) 45%, transparent);
	}
	:global(.badge.ok) {
		color: var(--ok);
		border-color: color-mix(in srgb, var(--ok) 45%, transparent);
	}
	:global(.banner) {
		padding: 10px 14px;
		border-radius: var(--radius);
		border: 1px solid var(--border);
		background: var(--panel-2);
	}
	:global(.banner.warn) {
		border-color: color-mix(in srgb, var(--warn) 50%, transparent);
		background: color-mix(in srgb, var(--warn) 10%, var(--panel));
	}
	:global(.banner.danger) {
		border-color: color-mix(in srgb, var(--danger) 50%, transparent);
		background: color-mix(in srgb, var(--danger) 10%, var(--panel));
	}
	:global(.muted) {
		color: var(--muted);
	}
</style>
