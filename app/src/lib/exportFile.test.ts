// The Tauri path of writeExport, with the fs plugin replaced by a fake
import { beforeEach, describe, expect, it, vi } from 'vitest';

const log: string[] = [];
let written = '';
vi.mock('./http', () => ({ inTauri: true }));
vi.mock('@tauri-apps/plugin-dialog', () => ({ save: vi.fn() }));
vi.mock('@tauri-apps/plugin-fs', () => ({
	open: vi.fn(async (path: string, opts: object) => {
		log.push(`open ${path} ${JSON.stringify(opts)}`);
		return {
			write: async (b: Uint8Array) => {
				written += new TextDecoder().decode(b);
				if (written.includes('BOOM')) throw new Error('disk full');
			},
			close: async () => void log.push('close'),
		};
	}),
	remove: vi.fn(async (path: string) => void log.push(`remove ${path}`)),
	exists: vi.fn(async () => true),
	mkdir: vi.fn(),
}));

const { writeExport } = await import('./exportFile');
const chunks = (...texts: string[]) => texts.map((text, i) => ({ text, progress: (i + 1) / texts.length }));

beforeEach(() => {
	log.length = 0;
	written = '';
});

describe('writeExport (Tauri)', () => {
	it('streams every chunk into the file, then closes it', async () => {
		const progress: number[] = [];
		const ok = await writeExport('/x/a.csv', chunks('a,', 'b,', 'c\n'), (p) => progress.push(p), () => false);
		expect(ok).toBe(true);
		expect(written).toBe('a,b,c\n');
		expect(progress).toEqual([1 / 3, 2 / 3, 1]);
		expect(log).toEqual(['open /x/a.csv {"write":true,"create":true,"truncate":true}', 'close']);
	});

	it('removes the partial file when cancelled', async () => {
		let n = 0;
		const ok = await writeExport('/x/b.csv', chunks('1', '2', '3'), () => n++, () => n >= 1);
		expect(ok).toBe(false);
		expect(written).toBe('1');
		expect(log.slice(1)).toEqual(['close', 'remove /x/b.csv']);
	});

	it('removes the partial file and rethrows on a write error', async () => {
		await expect(writeExport('/x/c.csv', chunks('ok', 'BOOM', 'never'), () => {}, () => false)).rejects.toThrow('disk full');
		expect(log.slice(1)).toEqual(['close', 'remove /x/c.csv']);
	});
});
