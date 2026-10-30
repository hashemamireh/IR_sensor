// Which layout the window's width calls for. Width, not aspect ratio: it decides
// whether the list and graph fit side by side, and a portrait tablet is simply a
// narrow window.
//   wide      ≥ 1000 px  list beside the graph (tablet landscape, desktop)
//   portrait  600–999 px graph on top, list below, compact top bar
//   phone     < 600 px   one screen at a time, panels as bottom sheets
// Component CSS that can't read this uses the same numbers in media queries.

export type Layout = 'wide' | 'portrait' | 'phone';

export const PHONE_BELOW = 600;
export const WIDE_FROM = 1000;

export function layoutFor(width: number): Layout {
	return width < PHONE_BELOW ? 'phone' : width < WIDE_FROM ? 'portrait' : 'wide';
}

class Screen {
	width = $state(typeof window === 'undefined' ? WIDE_FROM : window.innerWidth);

	constructor() {
		if (typeof window !== 'undefined') window.addEventListener('resize', () => (this.width = window.innerWidth));
	}

	get layout(): Layout {
		return layoutFor(this.width);
	}
	/** Portrait or phone: the top bar and graph header are condensed */
	get compact(): boolean {
		return this.layout !== 'wide';
	}
	get phone(): boolean {
		return this.layout === 'phone';
	}
}

export const screen = new Screen();
