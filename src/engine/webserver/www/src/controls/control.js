// A CONTROL — an element standing for one node of a form's view (docs/public/client-protocol.md, The frame): its type
// is the node's NodeType, its element <oes-…> the same name in lower case. The view is folded into the page control
// by control: an element is found by its control id, asked to bring itself up to the node, and drawn anew only when
// it says it cannot. What survives is what a redraw would throw away — the scroll, the focus, a table and its pages.
//
// A node carries what its control was saved with beside its State, what the server reads it as now; where both name
// a thing (Visible, Enabled, Tooltip) the State is what holds.

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

/**
 * What a control needs of the form it stands in: the form's Key, and the doors back to the server.
 * @typedef {object} FormContext
 * @property {string} key the form's Key — what an event names it by
 * @property {(control: number, kind: number, args?: object, subscribed?: number[]) => Promise<any>} send
 * @property {(control: number, request: object) => Promise<any>} fetch
 * @property {(picture: string | number | Uint8Array | undefined) => string} picture a picture's URL — by its id, or a
 *   file's PNG as its field holds it — '' when there is none
 * @property {(node: any) => Promise<void>} fetchPictures the pictures a node names, asked for before it is drawn — a
 *   table's page fetched after the frame
 */

// The events a control sends (ibEvent::ibKind).
export const EventKind = Object.freeze({
	Focus: 1, Command: 2, Press: 3, Input: 4, Change: 5, Select: 6, Open: 7, Clear: 8, Toggle: 9, Page: 10, Move: 11,
	Row: 12, Sort: 13, Cell: 14, Resize: 15, Action: 16,
});

/** @type {Map<string, string>} a node type → its element's tag */
const tags = new Map();

/**
 * @param {string} type the node's NodeType, as the server names its control (ibForm::ibType)
 * @param {CustomElementConstructor} control
 */
export function defineControl(type, control) {
	const tag = `oes-${type.toLowerCase()}`;
	tags.set(type, tag);
	customElements.define(tag, control);
}

export class Control extends HTMLElement {
	constructor() {
		super();
		/** @type {ProtocolNode} the node it was last brought up to, without its children */
		this.node = {};
		/** @type {FormContext} */
		this.context = /** @type {any} */ (null);
		/** @type {HTMLElement} where its colours go — the field, not the row with the caption beside it */
		this.lookTarget = this;
	}

	/** The first drawing. @param {ProtocolNode} node */
	build(node) {}

	/**
	 * Brought up to the node in place; false — it cannot be, and is drawn anew. The default stands as long as the
	 * node's own entries have not moved.
	 * @param {ProtocolNode} node
	 * @param {ProtocolNode} previous
	 */
	update(node, previous) {
		return sameEntries(node, previous);
	}

	/**
	 * Its children folded in: an ordinary container's are controls inside it. One that draws them its own way says so
	 * here — a table's columns are a header, not boxes stacked under it; a notebook's pages stand behind tabs.
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		reconcileChildren(this, node.NodeChildren ?? [], this.context);
	}

	/** Lets go of what it holds outside the page — a grid's instance. */
	dispose() {}

	/**
	 * How wide its label is, for the column it stands in (controls/labels.js); null — it takes no part. The thin
	 * client's question of a control with a label beside it (ibControlDynamicBorder::CalculateLabelSize).
	 * @returns {number | null}
	 */
	calculateLabelSize() {
		return null;
	}

	/**
	 * The column's width, given to its label (ibControlDynamicBorder::ApplyLabelSize).
	 * @param {number} width
	 */
	applyLabelSize(width) {}

	/** @param {ProtocolNode} node */
	state(node) {
		return node.State ?? {};
	}

	/**
	 * What the person did with it, sent as its event.
	 * @param {number} kind
	 * @param {object} [args]
	 */
	send(kind, args) {
		return this.context.send(this.node.ControlId, kind, args, this.state(this.node).Subscribed);
	}

	/**
	 * Applied to every control on every pass: shown and enabled, the tooltip, the colours, the font, the sizes.
	 * Everything it sets it also clears, so what went away from the node goes away here too.
	 * @param {ProtocolNode} node
	 */
	applyCommon(node) {
		const state = this.state(node);
		const visible = state.Visible ?? node.Visible;
		const enabled = state.Enabled ?? node.Enabled;
		this.hidden = visible === false;
		// Disabled is the whole control, the field inside it too: out of reach of a click and of Tab alike.
		this.toggleAttribute('disabled', enabled === false);
		this.inert = enabled === false;
		const tooltip = state.Tooltip ?? node.Tooltip;
		if (tooltip)
			this.title = tooltip;
		else
			this.removeAttribute('title');

		this.lookTarget.style.color = colour(node.ForegroundColour);
		this.lookTarget.style.backgroundColor = colour(node.BackgroundColour);
		applyFont(this, node.Font);

		// A stored minimum raises a floor; it does not lower one — max() keeps the stylesheet's own floor too.
		const minimum = readSize(node.MinimumSize);
		const maximum = readSize(node.MaximumSize);
		this.style.minWidth = minimum.width > 0 ? `max(${minimum.width}px, var(--oes-min-width, 0px))` : '';
		this.style.minHeight = minimum.height > 0 ? `max(${minimum.height}px, var(--oes-min-height, 0px))` : '';
		this.style.maxWidth = maximum.width > 0 ? `${maximum.width}px` : '';
		this.style.maxHeight = maximum.height > 0 ? `${maximum.height}px` : '';
	}
}

/** A control the page has no drawing for: named, so a form shows where it stands. */
export class UnknownControl extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.textContent = `[${node.NodeType}]`;
	}
}
customElements.define('oes-unknown', UnknownControl);

/**
 * The node folded into the element that stood for it (or none): the element that stands for it now.
 * @param {HTMLElement} parent
 * @param {Control | null} existing
 * @param {ProtocolNode} node
 * @param {FormContext} context
 * @returns {Control}
 */
export function reconcile(parent, existing, node, context) {
	const tag = tags.get(node.NodeType ?? '') ?? 'oes-unknown';
	let element = existing;
	if (element === null || element.localName !== tag || !element.update(node, element.node)) {
		const made = /** @type {Control} */ (document.createElement(tag));
		made.context = context;
		made.node = withoutChildren(node);
		made.build(node);
		if (element !== null) {
			if (element.parentNode === parent)
				parent.replaceChild(made, element);
			dispose(element);
		}
		else
			parent.appendChild(made);
		element = made;
	}
	element.context = context;
	element.node = withoutChildren(node);
	element.dataset.control = String(node.ControlId ?? '');
	element.applyCommon(node);
	element.drawChildren(node);
	return element;
}

/**
 * The children of an ordinary container, matched by their control id. An element the control put there itself (a
 * caption, a legend) carries none and stays in front.
 * @param {HTMLElement} parent
 * @param {ProtocolNode[]} children
 * @param {FormContext} context
 */
export function reconcileChildren(parent, children, context) {
	const spare = new Map();
	let lead = 0;
	for (const child of Array.from(parent.children)) {
		if (child instanceof Control)
			spare.set(child.dataset.control, child);
		else if (spare.size === 0)
			lead++;
	}
	const placed = children.map((node) => {
		const key = String(node.ControlId ?? '');
		const existing = spare.get(key) ?? null;
		spare.delete(key);
		return reconcile(parent, existing, node, context);
	});
	for (const gone of spare.values())
		dispose(gone);
	placed.forEach((element, index) => {
		const at = parent.children[lead + index] ?? null;
		if (at !== element)
			parent.insertBefore(element, at);
	});
}

/**
 * Takes the element out and lets every control under it let go of what it holds.
 * @param {Element} element
 */
export function dispose(element) {
	element.remove();
	for (const control of [element, ...element.querySelectorAll('*')]) {
		if (control instanceof Control)
			control.dispose();
	}
}

/** @param {ProtocolNode} node */
function withoutChildren(node) {
	const { NodeChildren, ...own } = node;
	return own;
}

/**
 * @param {ProtocolNode} node
 * @param {ProtocolNode} previous
 */
function sameEntries(node, previous) {
	return JSON.stringify(withoutChildren(node)) === JSON.stringify(previous);
}

/** A colour's text, `r,g,b` as a form writes it; '' when it names none. @param {string | undefined} text */
export function colour(text) {
	if (!text)
		return '';
	const parts = String(text).split(',').map(Number);
	return parts.length === 3 && parts.every((part) => part >= 0 && part <= 255) ? `rgb(${parts.join(',')})` : '';
}

/**
 * A size's text, `width,height`, -1 for none.
 * @param {string | undefined} text
 */
function readSize(text) {
	const [width, height] = String(text ?? '').split(',').map(Number);
	return { width: Number.isFinite(width) ? width : -1, height: Number.isFinite(height) ? height : -1 };
}

/**
 * A font's text put on an element — what it does not name is cleared.
 * @param {HTMLElement} element
 * @param {string | undefined} text
 */
export function applyFont(element, text) {
	const font = readFont(text);
	// The stored face is a Windows name for "the UI font" — Segoe UI, which no Mac or Linux has. Named alone it
	// matches nothing there and the browser falls back to a serif; the theme's own font stands behind it.
	element.style.fontFamily = font?.face ? `"${font.face.replace(/"/g, '')}", var(--oes-font-ui)` : '';
	element.style.fontSize = font && font.size > 0 ? `${font.size}pt` : '';
	element.style.fontWeight = font && font.weight !== 400 ? String(font.weight) : '';
	element.style.fontStyle = font?.italic ? 'italic' : '';
	element.style.textDecoration = font?.underlined ? 'underline' : '';
}

/**
 * A font's text — `face,style,weight,pointSize,family,underlined` (core/graphics/font.h); null when it names none.
 * @param {string | undefined} text
 */
function readFont(text) {
	if (!text)
		return null;
	const [face, style, weight, size, , underlined] = String(text).split(',');
	// The weights before wxWidgets 3.1.2: 90 normal, 91 light, 92 bold.
	const legacy = { 90: 400, 91: 300, 92: 700 };
	const numeric = Number(weight);
	return {
		face,
		italic: Number(style) === 93 || Number(style) === 94,
		weight: legacy[numeric] ?? (numeric >= 1 && numeric <= 1000 ? numeric : 400),
		size: Number(size),
		underlined: Number(underlined) === 1,
	};
}
