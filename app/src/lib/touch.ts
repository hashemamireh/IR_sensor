// Stops the webview's own pinch-zoom of the whole app. WebKitGTK (Fedora) ignores
// `user-scalable=no` and `touch-action` for its pinch gesture, but only runs it when
// the page leaves the touch unhandled — so any touch with two or more fingers is
// cancelled here. One-finger touches (scrolling lists and popovers) are untouched,
// and the graph keeps its own pinch: it reads pointer events, which arrive before
// these touch events and aren't affected by cancelling them.

const multiTouch = (e: TouchEvent) => {
	if (e.touches.length > 1) e.preventDefault();
};

// WKWebView (macOS / iOS) pinches through its own gesture events instead
const gesture = (e: Event) => e.preventDefault();

/** Installs the blockers; returns a function that removes them */
export function blockPageZoom(target: Document = document): () => void {
	const opts = { passive: false } as const;
	target.addEventListener('touchstart', multiTouch, opts);
	target.addEventListener('touchmove', multiTouch, opts);
	for (const type of ['gesturestart', 'gesturechange']) target.addEventListener(type, gesture, opts);
	return () => {
		target.removeEventListener('touchstart', multiTouch);
		target.removeEventListener('touchmove', multiTouch);
		for (const type of ['gesturestart', 'gesturechange']) target.removeEventListener(type, gesture);
	};
}
