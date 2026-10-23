// Where exports go and how they're written. In Tauri: a system Save dialog starting
// in <data dir>/exports, then the chunks are streamed into the file. In a plain
// browser the file is assembled in memory and downloaded.
import { save } from '@tauri-apps/plugin-dialog';
import * as fs from '@tauri-apps/plugin-fs';
import type { Chunk, ExportFormat } from './export';
import { inTauri } from './http';

const FILTERS: Record<ExportFormat, { name: string; extensions: string[] }> = {
	csv: { name: 'CSV', extensions: ['csv'] },
	json: { name: 'JSON', extensions: ['json'] },
};

/** Asks where to save; null if cancelled. In browser mode returns the file name. */
export async function chooseExportPath(dataDir: string, fileName: string, format: ExportFormat): Promise<string | null> {
	if (!inTauri) return fileName;
	const dir = `${dataDir}/exports`;
	try {
		if (!(await fs.exists(dir))) await fs.mkdir(dir, { recursive: true });
	} catch {
		// the dialog still works, just starting elsewhere
	}
	return save({ defaultPath: `${dir}/${fileName}`, filters: [FILTERS[format]] });
}

/**
 * Writes the chunks to `path`. `cancelled()` is checked between chunks; a
 * cancelled or failed export removes the partial file.
 */
export async function writeExport(
	path: string,
	chunks: Iterable<Chunk>,
	onProgress: (p: number) => void,
	cancelled: () => boolean,
): Promise<boolean> {
	const enc = new TextEncoder();
	if (!inTauri) {
		const parts: Uint8Array[] = [];
		for (const c of chunks) {
			if (cancelled()) return false;
			parts.push(enc.encode(c.text));
			onProgress(c.progress);
			await new Promise((r) => setTimeout(r));
		}
		const a = document.createElement('a');
		a.href = URL.createObjectURL(new Blob(parts as BlobPart[]));
		a.download = path;
		a.click();
		URL.revokeObjectURL(a.href);
		return true;
	}

	const file = await fs.open(path, { write: true, create: true, truncate: true });
	let ok = false;
	try {
		for (const c of chunks) {
			if (cancelled()) return false;
			await file.write(enc.encode(c.text));
			onProgress(c.progress);
		}
		ok = true;
		return true;
	} finally {
		await file.close();
		if (!ok) await fs.remove(path).catch(() => undefined);
	}
}
