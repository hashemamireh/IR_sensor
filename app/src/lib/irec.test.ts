import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { zlibSync } from 'fflate';
import { crc32, durationS, HEADER_SIZE, IrecError, parseHeader, parseIrec, sampleRateHz } from './irec';
import expected from './fixtures/expected.json';

// Fixtures come from the mock device's build_irec (scripts/make_fixtures.py)
const fixture = (name: string) => new Uint8Array(readFileSync(new URL(`./fixtures/${name}`, import.meta.url)));
const NAMES = ['plain.irec', 'compressed.irec', 'fractional_rate.irec', 'overrun.irec'] as const;

describe('parseIrec on mock fixtures', () => {
	for (const name of NAMES) {
		it(`reads ${name} like the Python reference`, () => {
			const want = expected[name];
			const f = parseIrec(fixture(name));
			expect(f.header.sampleRateMhz).toBe(want.sample_rate_mhz);
			expect(f.header.sampleCount).toBe(want.sample_count);
			expect(f.header.startUtcUs).toBe(want.start_utc_us);
			expect(f.header.crc32).toBe(want.crc32);
			expect(f.header.flags).toBe(want.flags);
			expect(f.header.attenTenthsDb).toBe(want.atten_tenths_db);
			expect(f.crcOk).toBe(true);
			expect(f.samples.length).toBe(want.sample_count);
			expect(Array.from(f.samples.subarray(0, 8))).toEqual(want.first);
			expect(f.samples[f.samples.length - 1]).toBe(want.last);
			let sum = 0, min = Infinity, max = -Infinity;
			for (const s of f.samples) { sum += s; min = Math.min(min, s); max = Math.max(max, s); }
			expect([sum, min, max]).toEqual([want.sum, want.min, want.max]);
		});
	}

	it('decodes the 12 dB attenuation byte', () => {
		expect(parseHeader(fixture('plain.irec')).attenTenthsDb).toBe(120);
	});

	it('gives the same samples and CRC compressed and uncompressed', () => {
		const a = parseIrec(fixture('plain.irec'));
		const b = parseIrec(fixture('compressed.irec'));
		expect(a.header.compressed).toBe(false);
		expect(b.header.compressed).toBe(true);
		expect(b.crc32).toBe(a.crc32);
		expect(b.samples).toEqual(a.samples);
	});

	it('flags overrun recordings', () => {
		expect(parseIrec(fixture('overrun.irec')).header.overrun).toBe(true);
		expect(parseIrec(fixture('plain.irec')).header.overrun).toBe(false);
	});

	it('uses the actual rate in millihertz for the timebase', () => {
		const h = parseHeader(fixture('fractional_rate.irec'));
		expect(sampleRateHz(h)).toBeCloseTo(10004.3, 6);
		expect(durationS(h)).toBeCloseTo(3000 / 10004.3, 9);
	});
});

describe('parseIrec on damaged files', () => {
	it('reports a CRC mismatch without throwing', () => {
		const bytes = fixture('plain.irec');
		bytes[HEADER_SIZE + 100] ^= 0x01;
		expect(parseIrec(bytes).crcOk).toBe(false);
	});

	it('rejects bad magic, a short header and an unknown version', () => {
		const bad = fixture('plain.irec');
		bad[0] = 0x58;
		expect(() => parseIrec(bad)).toThrow(IrecError);
		expect(() => parseIrec(fixture('plain.irec').subarray(0, 20))).toThrow(/shorter/);
		const v2 = fixture('plain.irec');
		v2[4] = 2;
		expect(() => parseIrec(v2)).toThrow(/version 2/);
	});

	it('rejects a truncated payload', () => {
		expect(() => parseIrec(fixture('plain.irec').subarray(0, 1000))).toThrow(/Payload/);
	});

	it('rejects a corrupt zlib stream', () => {
		const bytes = fixture('compressed.irec');
		bytes.fill(0xff, HEADER_SIZE + 2, HEADER_SIZE + 40);
		expect(() => parseIrec(bytes)).toThrow(IrecError);
	});

	it('handles a payload at an odd byte offset', () => {
		const src = fixture('plain.irec');
		const shifted = new Uint8Array(src.length + 1);
		shifted.set(src, 1);
		expect(parseIrec(shifted.subarray(1)).crcOk).toBe(true);
	});
});

describe('crc32', () => {
	it('matches the standard check value', () => {
		expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
	});

	it('round-trips a TS-built compressed file', () => {
		const plain = fixture('plain.irec');
		const packed = zlibSync(plain.subarray(HEADER_SIZE));
		const file = new Uint8Array(HEADER_SIZE + packed.length);
		file.set(plain.subarray(0, HEADER_SIZE));
		file[0x19] |= 0x01;
		file.set(packed, HEADER_SIZE);
		expect(parseIrec(file).crcOk).toBe(true);
	});
});
