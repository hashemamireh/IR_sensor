import { describe, expect, it } from 'vitest';
import type { DeviceRecording } from './api';
import { baseName, mergeRecordings, missingLocally, usableCalibration, type LocalRecording } from './recordings';

const dev = (id: number, start: number): DeviceRecording => ({
	id,
	start_utc_us: start,
	duration_ms: 5000,
	sample_rate_mhz: 10_000_000,
	sample_count: 50_000,
	size_bytes: 100_032,
	compressed: false,
	overrun: false,
	attenuation_db: 12,
});

const local = (ssid: string, id: number, start: number): LocalRecording => ({
	...dev(id, start),
	schema: 1,
	file: `${baseName(ssid, id)}.irec`,
	sidecar: `${baseName(ssid, id)}.json`,
	device_ssid: ssid,
	downloaded_utc_us: 0,
	crc32: 0,
	crc_ok: true,
	calibration: null,
	name: null,
	notes: '',
	tags: [],
});

describe('file names', () => {
	it('pads the ID and sanitises the SSID', () => {
		expect(baseName('IRSensor-AB12', 42)).toBe('IRSensor-AB12_00042');
		expect(baseName('a b/c', 7, 2)).toBe('a_b_c_00007-2');
	});
});

describe('mergeRecordings', () => {
	it('matches device and tablet copies by SSID, ID and start time', () => {
		const rows = mergeRecordings('IRSensor-A', [dev(1, 100), dev(2, 200)], [local('IRSensor-A', 1, 100)]);
		expect(rows.map((r) => [r.id, !!r.device, !!r.local])).toEqual([
			[2, true, false],
			[1, true, true],
		]);
	});

	it('keeps a reused ID (NVS erased) and other devices apart', () => {
		const rows = mergeRecordings(
			'IRSensor-A',
			[dev(1, 900)],
			[local('IRSensor-A', 1, 100), local('IRSensor-B', 1, 900)],
		);
		expect(rows).toHaveLength(3);
		expect(rows.filter((r) => r.device && r.local)).toHaveLength(0);
	});

	it('lists only tablet copies while offline', () => {
		expect(mergeRecordings(null, null, [local('IRSensor-A', 3, 5)]).map((r) => r.id)).toEqual([3]);
	});
});

describe('missingLocally', () => {
	it('returns device recordings without a tablet copy, oldest first', () => {
		const missing = missingLocally('S', [dev(5, 50), dev(3, 30), dev(4, 40)], [local('S', 4, 40)]);
		expect(missing.map((d) => d.id)).toEqual([3, 5]);
	});
});

describe('usableCalibration', () => {
	const cal = { attenuation_db: 12, mv: new Array(4096).fill(0) };
	it('uses a table only for the attenuation in the file header', () => {
		expect(usableCalibration({ calibration: cal }, 120)).toBe(cal);
		expect(usableCalibration({ calibration: cal }, 25)).toBeNull();
		expect(usableCalibration({ calibration: { attenuation_db: 2.5, mv: cal.mv } }, 25)).not.toBeNull();
		expect(usableCalibration({ calibration: null }, 120)).toBeNull();
	});
});
