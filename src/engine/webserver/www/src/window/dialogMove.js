// A DIALOG MOVES BY ITS TITLE, as a window on a desktop does: every wa-dialog of the page, whoever made it — held from the
// document, so none has to ask. It stays inside the window; opened again, it is centred again.

/** @type {WeakMap<HTMLElement, {x: number, y: number}>} how far a dialog's panel stands from where it opened */
const moved = new WeakMap();

/** A press on a dialog's title takes the dialog. To be called once, as the page starts. */
export function moveDialogs() {
	document.addEventListener('pointerdown', (event) => {
		if (event.button !== 0)
			return;
		const path = /** @type {Element[]} */ (event.composedPath().filter((one) => one instanceof Element));
		const dialog = path.find((one) => one.localName === 'wa-dialog');
		const header = path.find((one) => one.part.contains('header'));
		// The close button in the title is a button.
		if (dialog === undefined || header === undefined || path.some((one) => one.localName === 'wa-button'))
			return;
		const panel = /** @type {HTMLElement | null} */ (dialog.shadowRoot?.querySelector('[part~="dialog"]') ?? null);
		if (panel !== null)
			drag(/** @type {HTMLElement} */ (header), panel, event);
	});
}

/**
 * The panel follows the pointer until it is let go, kept inside the window.
 * @param {HTMLElement} header
 * @param {HTMLElement} panel
 * @param {PointerEvent} press
 */
function drag(header, panel, press) {
	const from = moved.get(panel) ?? { x: 0, y: 0 };
	const box = panel.getBoundingClientRect();
	const room = {
		left: Math.min(0, -box.left), right: Math.max(0, innerWidth - box.right),
		up: Math.min(0, -box.top), down: Math.max(0, innerHeight - box.bottom),
	};
	const follow = (/** @type {PointerEvent} */ event) => {
		const x = from.x + Math.min(room.right, Math.max(room.left, event.clientX - press.clientX));
		const y = from.y + Math.min(room.down, Math.max(room.up, event.clientY - press.clientY));
		moved.set(panel, { x, y });
		panel.style.translate = `${x}px ${y}px`;
	};
	const letGo = () => {
		header.removeEventListener('pointermove', follow);
		header.removeEventListener('pointerup', letGo);
		header.removeEventListener('pointercancel', letGo);
	};
	header.setPointerCapture(press.pointerId);
	header.addEventListener('pointermove', follow);
	header.addEventListener('pointerup', letGo);
	header.addEventListener('pointercancel', letGo);
}
