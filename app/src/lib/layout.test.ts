import { describe, expect, it } from 'vitest';
import { layoutFor } from './layout.svelte';

describe('layoutFor', () => {
	it('picks the layout from the width', () => {
		expect(layoutFor(360)).toBe('phone');
		expect(layoutFor(599)).toBe('phone');
		expect(layoutFor(600)).toBe('portrait');
		expect(layoutFor(853)).toBe('portrait'); // Surface Pro upright at 150 %
		expect(layoutFor(999)).toBe('portrait');
		expect(layoutFor(1000)).toBe('wide');
		expect(layoutFor(1280)).toBe('wide');
	});
});
