import { describe, expect, it } from 'vitest';
import { maxDurationS, SAMPLE_RATES_HZ } from './api';

describe('recording limits (spec §3.2, §4.4)', () => {
	it('offers every nominal rate the firmware accepts', () => {
		expect(SAMPLE_RATES_HZ).toEqual([1000, 2000, 5000, 10000, 20000]);
	});

	it('caps 20 kHz at 120 s and every other rate at 300 s', () => {
		expect(SAMPLE_RATES_HZ.map(maxDurationS)).toEqual([300, 300, 300, 300, 120]);
	});
});
