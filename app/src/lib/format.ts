// Display formatting. Device times are UTC µs; the app shows local time (decision D7).

/** 2024-01-01T00:00:00Z — the firmware's plausibility floor for POST /api/time */
const MIN_PLAUSIBLE_UTC_US = 1_704_067_200_000_000;

/** False for a timestamp taken before the device clock was ever set (e.g. 1970) */
export const clockWasSet = (utcUs: number) => utcUs >= MIN_PLAUSIBLE_UTC_US;

export function localTime(utcUs: number): string {
	if (!clockWasSet(utcUs)) return 'clock not set';
	return new Date(utcUs / 1000).toLocaleString(undefined, {
		year: 'numeric',
		month: 'short',
		day: 'numeric',
		hour: '2-digit',
		minute: '2-digit',
		second: '2-digit',
	});
}

export function utcTime(utcUs: number): string {
	return clockWasSet(utcUs) ? new Date(utcUs / 1000).toISOString().replace('T', ' ').replace(/\.\d+Z$/, ' UTC') : 'device clock was not set';
}

export function bytes(n: number): string {
	if (n < 1024) return `${n} B`;
	if (n < 1024 * 1024) return `${(n / 1024).toFixed(0)} KB`;
	return `${(n / 1024 / 1024).toFixed(1)} MB`;
}

export function duration(ms: number): string {
	const s = ms / 1000;
	if (s < 60) return `${s.toFixed(s < 10 ? 2 : 1)} s`;
	const m = Math.floor(s / 60);
	return `${m} min ${Math.round(s - m * 60)} s`;
}

/** Nominal-looking rates as "10 kHz"; measured ones keep their digits, e.g. "10.0043 kHz" */
export function rate(mhz: number): string {
	const hz = mhz / 1000;
	if (hz >= 1000) return `${+(hz / 1000).toFixed(4)} kHz`;
	return `${+hz.toFixed(3)} Hz`;
}

export function errorText(e: unknown): string {
	if (e instanceof Error) return e.message;
	return String(e);
}
