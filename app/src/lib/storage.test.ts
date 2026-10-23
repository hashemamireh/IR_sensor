// LocalStore with its in-memory backend (what a plain browser / Node gets)
import { describe, expect, it } from 'vitest';
import type { DeviceRecording } from './api';
import { LocalStore } from './storage';

const entry = (id: number, start: number): DeviceRecording => ({
	id,
	start_utc_us: start,
	duration_ms: 1000,
	sample_rate_mhz: 1_000_000,
	sample_count: 1000,
	size_bytes: 2032,
	compressed: false,
	overrun: false,
	attenuation_db: 12,
});
const crc = { crc32: 1, ok: true };

describe('LocalStore', () => {
	it('keeps name, tags and notes when the same recording is saved again', async () => {
		const store = await LocalStore.open();
		const first = await store.save('IRSensor-A', entry(3, 100), new Uint8Array(4), crc, null);
		await store.updateMeta(first, { name: 'Bench', tags: ['laser'], notes: 'n' });
		const again = await store.save('IRSensor-A', entry(3, 100), new Uint8Array(4), crc, null);
		expect(again.sidecar).toBe('IRSensor-A_00003.json');
		expect([again.name, again.tags, again.notes]).toEqual(['Bench', ['laser'], 'n']);
		const all = await store.loadAll();
		expect(all).toHaveLength(1);
		expect(all[0].name).toBe('Bench');
	});

	it('gives a different recording with the same ID its own file', async () => {
		const store = await LocalStore.open();
		await store.save('IRSensor-A', entry(3, 100), new Uint8Array(4), crc, null);
		const other = await store.save('IRSensor-A', entry(3, 999), new Uint8Array(4), crc, null);
		expect(other.sidecar).toBe('IRSensor-A_00003-2.json');
		expect(other.name).toBeNull();
		expect(await store.loadAll()).toHaveLength(2);
	});
});
