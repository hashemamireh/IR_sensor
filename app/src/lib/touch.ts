// Stops the webview's own pinch-zoom of the whole app. WebKitGTK (Fedora) ignores
// `user-scalable=no` and `touch-action` for its pinch gesture, but only runs it when
// the page leaves the touch unhandled — so any touch with two or more fingers is
// cancelled here. One-finger touches (scrolling lists and popovers) are untouched,
// and the graph keeps its own pinch: it reads pointer events, which arrive before
// these touch events and aren't affected by cancelling them.
//
// macOS trackpad pinches are left alone: they reach the graph as ctrl+wheel events,
// and WKWebView stops sending those once its gesture events are cancelled. The app's
// WKWebView doesn't magnify the page anyway (Tauri leaves magnification off).

const multiTouch = (e: TouchEvent) => {
	if (e.touches.length > 1) e.preventDefault();
};

/** Installs the blockers; returns a function that removes them */
export function blockPageZoom(target: Document = document): () => void {
	const opts = { passive: false } as const;
	target.addEventListener('touchstart', multiTouch, opts);
	target.addEventListener('touchmove', multiTouch, opts);
	return () => {
		target.removeEventListener('touchstart', multiTouch);
		target.removeEventListener('touchmove', multiTouch);
	};
}
