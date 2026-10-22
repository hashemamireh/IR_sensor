// Typed client for the device REST API (spec §8.2). Shapes follow the firmware's
// api_model.cpp, which the mock (tools/mock_device) copies.
import { fetchWithTimeout } from './http';

export type DeviceState = 'idle' | 'recording' | 'saving' | 'compressing';

export interface Status {
	state: DeviceState;
	battery: { mv: number; pct: number };
	free_bytes: number;
	recording_count: number;
	wifi: { ssid: string; client_connected: boolean };
	recording?: { id: number; elapsed_s: number; total_s: number };
	compress?: { done: number; total: number };
}

/** One entry of GET /api/recordings */
export interface DeviceRecording {
	id: number;
	start_utc_us: number;
	duration_ms: number;
	sample_rate_mhz: number;
	sample_count: number;
	size_bytes: number;
	compressed: boolean;
	overrun: boolean;
	/** A number, e.g. 12 or 2.5 */
	attenuation_db: number;
}

export interface Calibration {
	attenuation_db: number;
	/** 4096 entries, raw count → mV */
	mv: number[];
}

export const SAMPLE_RATES_HZ = [1000, 5000, 10000, 20000] as const;
export const MAX_DURATION_S = 300;
export const MAX_DURATION_20K_S = 120;
export const maxDurationS = (rateHz: number) => (rateHz === 20000 ? MAX_DURATION_20K_S : MAX_DURATION_S);

/** A non-2xx answer: `code` is the device's {"error": …} string (spec §8.2 error table) */
export class ApiError extends Error {
	constructor(
		readonly status: number,
		readonly code: string,
		readonly body: Record<string, unknown>,
	) {
		super(describeError(status, code, body));
	}
	get busy() {
		return this.status === 409;
	}
}

const MESSAGES: Record<string, string> = {
	busy: 'The device is busy',
	insufficient_space: 'Not enough free space on the device',
	invalid_rate: 'Unsupported sample rate',
	invalid_duration: 'Duration is out of range for this sample rate',
	invalid_time: 'The device rejected the time',
	not_found: 'Not found on the device',
	storage_error: 'The device had a storage error',
	calibration_unavailable: 'This chip has no ADC calibration data',
	confirmation_required: 'The device asked for confirmation',
};

function describeError(status: number, code: string, body: Record<string, unknown>) {
	const msg = MESSAGES[code] ?? `HTTP ${status} ${code}`;
	return code === 'busy' && body.state ? `${msg} (${body.state})` : msg;
}

// Timeouts. The device serves one request at a time, so a status poll can wait
// behind a download; the app serialises its own requests (device.svelte.ts).
const TIMEOUT_MS = 4000;
const DOWNLOAD_TIMEOUT_MS = 120_000;
// Safety margin: the firmware used to open every file for its header to build the
// list. RAM-cached headers should make it fast; keep this until bring-up confirms it.
const LIST_TIMEOUT_MS = 30_000;

export class DeviceApi {
	constructor(readonly baseUrl: string) {}

	private url(path: string) {
		return this.baseUrl.replace(/\/+$/, '') + path;
	}

	private async json<T>(method: string, path: string, body?: unknown, timeoutMs = TIMEOUT_MS): Promise<T> {
		const init: RequestInit = { method };
		if (body !== undefined) {
			init.body = JSON.stringify(body);
			init.headers = { 'Content-Type': 'application/json' };
		}
		return fetchWithTimeout(this.url(path), init, timeoutMs, async (res) => {
			const text = await res.text();
			let parsed: Record<string, unknown> = {};
			try {
				parsed = text ? JSON.parse(text) : {};
			} catch {
				// esp_http_server's built-in 404/405 pages are plain text
			}
			if (!res.ok) throw new ApiError(res.status, String(parsed.error ?? res.statusText), parsed);
			return parsed as T;
		});
	}

	status() {
		return this.json<Status>('GET', '/api/status');
	}

	startRecording(durationS: number, sampleRateHz: number) {
		return this.json<{ id: number; duration_s: number; sample_rate_hz: number }>('POST', '/api/record', {
			duration_s: durationS,
			sample_rate_hz: sampleRateHz,
		});
	}

	stopRecording() {
		return this.json<{ id: number }>('POST', '/api/record/stop');
	}

	async listRecordings(): Promise<DeviceRecording[]> {
		return (await this.json<{ recordings: DeviceRecording[] }>('GET', '/api/recordings', undefined, LIST_TIMEOUT_MS))
			.recordings;
	}

	/** The raw .irec bytes, exactly as stored on the device */
	downloadRecording(id: number): Promise<Uint8Array> {
		return fetchWithTimeout(this.url(`/api/recordings/${id}`), { method: 'GET' }, DOWNLOAD_TIMEOUT_MS, async (res) => {
			if (!res.ok) {
				let parsed: Record<string, unknown> = {};
				try {
					parsed = await res.json();
				} catch {
					// not JSON
				}
				throw new ApiError(res.status, String(parsed.error ?? res.statusText), parsed);
			}
			return new Uint8Array(await res.arrayBuffer());
		});
	}

	deleteRecording(id: number) {
		return this.json<{ deleted: number }>('DELETE', `/api/recordings/${id}`);
	}

	deleteAllRecordings() {
		return this.json<{ deleted: number }>('DELETE', '/api/recordings', { confirm: 'DELETE_ALL' });
	}

	syncTime(utcUs = Date.now() * 1000) {
		return this.json<{ utc_us: number }>('POST', '/api/time', { utc_us: utcUs });
	}

	calibration() {
		return this.json<Calibration>('GET', '/api/calibration', undefined, 10_000);
	}
}
