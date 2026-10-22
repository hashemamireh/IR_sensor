// One fetch for the whole app. In Tauri, requests go out through the HTTP plugin
// (Rust side), so the device's lack of CORS headers doesn't matter. In a plain
// browser (`pnpm dev`, handy against the mock, which does send CORS) it falls back
// to window.fetch.
import { fetch as tauriFetch } from '@tauri-apps/plugin-http';

export const inTauri = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

const httpFetch: typeof fetch = inTauri ? (tauriFetch as typeof fetch) : (...a) => fetch(...a);

export class TimeoutError extends Error {}

/** fetch + read the body within one total timeout, in ms */
export async function fetchWithTimeout<T>(
	url: string,
	init: RequestInit,
	timeoutMs: number,
	read: (res: Response) => Promise<T>,
): Promise<T> {
	const ctrl = new AbortController();
	let timedOut = false;
	const timer = setTimeout(() => {
		timedOut = true;
		ctrl.abort();
	}, timeoutMs);
	try {
		return await read(await httpFetch(url, { ...init, signal: ctrl.signal }));
	} catch (e) {
		throw timedOut ? new TimeoutError(`No answer within ${timeoutMs / 1000} s`) : e;
	} finally {
		clearTimeout(timer);
	}
}
