// A PROPERTY'S CELL — what a row of a property set is edited with, by the server's class of it (the row's Class, as the
// thin client's ibPropertyRegistry maps it; docs/private/web/browser-parity.md, D1). A maker is given the row and a
// door: it reads the row's Value and hands a new one through the door, in the same shape — the node is the value, so a
// cell needs no variant and no pair of conversions — and, where it knows, how the new one reads (its Presentation).
// The cells are registered by mode: runtime.js those the application is sent; a designer's page would register its
// own beside them, as appstudio does on the thin side. A class nobody registered is shown, never guessed: its
// Presentation, read-only.

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {(value: any, presentation?: string) => void} PropertyChange */
/** @typedef {(row: ProtocolNode, change: PropertyChange) => HTMLElement} PropertyMaker */

/** @type {Map<string, PropertyMaker>} a property's class → the maker of its cell */
const makers = new Map();

/**
 * @param {string} className the server's class of the property — ibPropertyBoolean, ibPropertyEnum…
 * @param {PropertyMaker} maker
 */
export function defineProperty(className, maker) {
	makers.set(className, maker);
}

/**
 * The row's cell; out of reach when the row is read-only.
 * @param {ProtocolNode} row
 * @param {PropertyChange} change a new value, in the shape the row holds it
 * @returns {HTMLElement}
 */
export function cellOf(row, change) {
	const maker = makers.get(row.Class ?? '');
	if (maker === undefined) {
		const text = document.createElement('span');
		text.className = 'property-text';
		text.textContent = row.Presentation ?? '';
		return text;
	}
	const cell = maker(row, change);
	cell.inert = row.ReadOnly === true;
	return cell;
}
