// THE START PAGE — one tab showing the forms the configuration places on it, all of them live (docs/public/client-protocol.md,
// a tab's Kind; the thin client's ibHomePageView): its columns side by side, the cells of each one under the other, a
// strip over each form saying what lives there, a sash between them to drag. The server reads its workspace as the page
// opens and keeps it for its life, so it is laid out once; each cell's form is folded in after that as a tab's is.

import { Control, reconcile } from '../controls/control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../controls/control.js').FormContext} FormContext */

// The least a sash leaves a column and a cell, the thin client's.
const columnAtLeast = 80;
const cellAtLeast = 40;

export class HomePage extends Control {
	constructor() {
		super();
		/** @type {{element: HTMLElement, icon: HTMLImageElement, caption: HTMLElement, unavailable: HTMLElement, form: Control | null}[]} */
		this.cells = [];
	}

	/**
	 * Brought up to the view: laid out the first time, then each cell in the order it was laid out.
	 * @param {ProtocolNode} view
	 * @param {(form: ProtocolNode) => FormContext} contextOf
	 * @param {Pictures} pictures
	 */
	draw(view, contextOf, pictures) {
		const columns = (view.Columns?.NodeChildren ?? []).filter((column) => (column.NodeChildren?.length ?? 0) > 0);
		if (this.cells.length === 0)
			this.layOut(columns, view.Gravity);
		columns.flatMap((column) => column.NodeChildren ?? []).forEach((node, place) => {
			const cell = this.cells[place];
			if (cell === undefined)
				return;
			const title = node.Title ?? '';
			cell.caption.textContent = title;
			cell.caption.title = title;
			const icon = pictures.url(node.Icon);
			if (icon && cell.icon.getAttribute('src') !== icon)
				cell.icon.src = icon;
			cell.icon.hidden = !icon;
			cell.unavailable.hidden = node.View !== undefined;
			cell.unavailable.textContent = node.Unavailable ?? '';
			if (node.View !== undefined)
				cell.form = reconcile(cell.element, cell.form, node.View, contextOf(node.View));
		});
	}

	/**
	 * The columns and their cells, each with its share: a column's of the width by the template's proportion, a cell's of
	 * its column by its Height.
	 * @param {ProtocolNode[]} columns
	 * @param {number | undefined} gravity the left column's share
	 */
	layOut(columns, gravity) {
		const left = gravity !== undefined && gravity > 0 && gravity < 1 ? gravity : 0.5;
		this.append(...between(columns.map((column, place) => {
			const element = document.createElement('div');
			element.className = 'home-column';
			share(element, columns.length > 1 ? (place === 0 ? left : 1 - left) : 1);
			element.append(...between((column.NodeChildren ?? []).map((node) => this.layOutCell(node)), 'vertical', cellAtLeast));
			return element;
		}), 'horizontal', columnAtLeast));
	}

	/**
	 * A cell: its strip, and under it its form or what is said in its place.
	 * @param {ProtocolNode} node
	 */
	layOutCell(node) {
		const element = document.createElement('section');
		element.className = 'home-cell';
		share(element, node.Height > 0 ? node.Height : 1);
		const strip = document.createElement('div');
		strip.className = 'home-strip';
		const icon = document.createElement('img');
		icon.alt = '';
		const caption = document.createElement('span');
		strip.append(icon, caption);
		const unavailable = document.createElement('div');
		unavailable.className = 'home-unavailable';
		element.append(strip, unavailable);
		this.cells.push({ element, icon, caption, unavailable, form: null });
		return element;
	}
}
customElements.define('oes-homepage', HomePage);

/**
 * A pane's share of what its parent has to give.
 * @param {HTMLElement} pane
 * @param {number} value
 */
function share(pane, value) {
	pane.style.setProperty('--share', String(value));
}

/**
 * The panes with a sash between each two.
 * @param {HTMLElement[]} panes
 * @param {'horizontal' | 'vertical'} direction the way the panes follow each other
 * @param {number} atLeast
 * @returns {HTMLElement[]}
 */
function between(panes, direction, atLeast) {
	return panes.flatMap((pane, place) => place === 0 ? [pane] : [sash(panes[place - 1], pane, direction, atLeast), pane]);
}

/**
 * The edge between two panes, dragged to give the one what the other loses. The shares become the panes' sizes as it is
 * taken: from then on they are what a resize of the window keeps in proportion, a person's drag with them.
 * @param {HTMLElement} before
 * @param {HTMLElement} after
 * @param {'horizontal' | 'vertical'} direction
 * @param {number} atLeast
 */
function sash(before, after, direction, atLeast) {
	const edge = document.createElement('div');
	edge.className = `home-sash ${direction}`;
	edge.setAttribute('role', 'separator');
	edge.setAttribute('aria-orientation', direction === 'horizontal' ? 'vertical' : 'horizontal');
	/** @param {Element} pane */
	const size = (pane) => direction === 'horizontal' ? pane.getBoundingClientRect().width : pane.getBoundingClientRect().height;
	/** @param {PointerEvent} event */
	const place = (event) => direction === 'horizontal' ? event.clientX : event.clientY;
	edge.addEventListener('pointerdown', (event) => {
		event.preventDefault();
		edge.setPointerCapture(event.pointerId);
		const from = place(event);
		const first = size(before);
		const both = first + size(after);
		// All measured before any is written: a share written moves the panes beside it.
		const panes = /** @type {HTMLElement[]} */ (Array.from(/** @type {HTMLElement} */ (edge.parentElement).children))
			.filter((pane) => !pane.classList.contains('home-sash'));
		const sizes = panes.map(size);
		panes.forEach((pane, at) => share(pane, sizes[at]));
		/** @param {PointerEvent} moved */
		const drag = (moved) => {
			const given = Math.max(atLeast, Math.min(first + place(moved) - from, both - atLeast));
			share(before, given);
			share(after, both - given);
		};
		edge.addEventListener('pointermove', drag);
		edge.addEventListener('lostpointercapture', () => edge.removeEventListener('pointermove', drag), { once: true });
	});
	return edge;
}
