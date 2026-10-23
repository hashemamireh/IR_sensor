import { describe, expect, it } from 'vitest';
import { downsampleChoices, estimateBytes, exportChunks, rowCount, type ExportOptions, type ExportSource } from './export';

const src = (n: number, lut = true): ExportSource => ({
	samples: Uint16Array.from({ length: n }, (_, i) => 100 + (i % 3000)),
	rateHz: 1000,
	lut: lut ? Float32Array.from({ length: 4096 }, (_, i) => i * 2) : null,
	meta: { id: 7, device_ssid: 'IRSensor-T' },
});
const text = (s: ExportSource, o: ExportOptions) => [...exportChunks(s, o)].map((c) => c.text).join('');

describe('CSV export', () => {
	it('writes full-resolution rows with sample times in ms', () => {
		const csv = text(src(5), { format: 'csv', i0: 0, i1: 5, factor: 1 });
		expect(csv.split('\n')).toEqual([
			'timestamp_ms,raw_adc,voltage_mv',
			'0.000,100,200',
			'1.000,101,202',
			'2.000,102,204',
			'3.000,103,206',
			'4.000,104,208',
			'',
		]);
	});

	it('keeps times relative to the recording start for a window', () => {
		const csv = text(src(10), { format: 'csv', i0: 4, i1: 6, factor: 1 });
		expect(csv.split('\n').slice(1, 3)).toEqual(['4.000,104,208', '5.000,105,210']);
	});

	it('averages blocks, timed at the block centre, including a short last block', () => {
		const csv = text(src(7), { format: 'csv', i0: 0, i1: 7, factor: 3 });
		expect(csv.split('\n').slice(1, 4)).toEqual([
			'1.000,101.00,202.00', // samples 0–2
			'4.000,104.00,208.00', // 3–5
			'6.000,106.00,212.00', // 6 alone
		]);
	});

	it('leaves voltage_mv empty without calibration', () => {
		const csv = text(src(2, false), { format: 'csv', i0: 0, i1: 2, factor: 1 });
		expect(csv.split('\n')[1]).toBe('0.000,100,');
	});

	it('streams large exports in chunks with rising progress', () => {
		const chunks = [...exportChunks(src(50_000), { format: 'csv', i0: 0, i1: 50_000, factor: 1 })];
		expect(chunks.length).toBeGreaterThan(2);
		expect(chunks.at(-1)!.progress).toBe(1);
		expect(chunks.map((c) => c.text).join('').split('\n').length).toBe(50_002);
	});
});

describe('JSON export', () => {
	it('is valid JSON with metadata, rates and both arrays', () => {
		const j = JSON.parse(text(src(50_000), { format: 'json', i0: 10, i1: 50_000, factor: 4 }));
		expect(j.format).toBe('irsensor-export');
		expect(j.recording.id).toBe(7);
		expect(j.sample_rate_hz).toBe(250);
		expect(j.row_count).toBe(rowCount({ i0: 10, i1: 50_000, factor: 4 }));
		expect(j.raw_adc).toHaveLength(j.row_count);
		expect(j.voltage_mv).toHaveLength(j.row_count);
		expect(j.raw_adc[0]).toBe(111.5); // samples 10–13
		expect(j.first_timestamp_ms).toBe(11.5);
		expect(j.timestamp_step_ms).toBe(4);
		expect(j.calibration_mv).toHaveLength(4096);
	});

	it('writes null for a count beyond the calibration table', () => {
		const s = src(2);
		s.samples[1] = 5000;
		const j = JSON.parse(text(s, { format: 'json', i0: 0, i1: 2, factor: 1 }));
		expect(j.voltage_mv).toEqual([200, null]);
	});

	it('writes voltage_mv: null without calibration', () => {
		const j = JSON.parse(text(src(3, false), { format: 'json', i0: 0, i1: 3, factor: 1 }));
		expect(j.raw_adc).toEqual([100, 101, 102]);
		expect(j.voltage_mv).toBeNull();
		expect(j.calibration_mv).toBeNull();
	});
});

describe('estimateBytes', () => {
	for (const format of ['csv', 'json'] as const) {
		for (const factor of [1, 10]) {
			it(`is within 5% for ${format}, factor ${factor}`, () => {
				const s = src(40_000);
				const o: ExportOptions = { format, i0: 0, i1: 40_000, factor };
				const real = new TextEncoder().encode(text(s, o)).length;
				expect(Math.abs(estimateBytes(s, o) - real) / real).toBeLessThan(0.05);
			});
		}
	}
});

describe('downsampleChoices', () => {
	it('offers common rates below the source as whole-number factors', () => {
		expect(downsampleChoices(20_000).map((c) => [c.label, c.factor])).toEqual([
			['10 kHz', 2], ['5 kHz', 4], ['2 kHz', 10], ['1 kHz', 20], ['500 Hz', 40], ['200 Hz', 100], ['100 Hz', 200],
		]);
		expect(downsampleChoices(2000).map((c) => [c.label, c.factor])).toEqual([
			['1 kHz', 2], ['500 Hz', 4], ['200 Hz', 10], ['100 Hz', 20],
		]);
		expect(downsampleChoices(1000).map((c) => c.label)).toEqual(['500 Hz', '200 Hz', '100 Hz']);
		// A measured rate keeps whole factors
		expect(downsampleChoices(10_004.3)[0]).toEqual({ factor: 2, label: '5 kHz' });
	});
});
