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
 * @property {() => boolean} studioMode the session is the studio's (Session.studioMode) — what a control draws
 *   differently there
 */

// The events a control sends (ibEvent::ibKind).
export const EventKind = Object.freeze({
	Focus: 1, Command: 2, Press: 3, Input: 4, Change: 5, Select: 6, Open: 7, Clear: 8, Toggle: 9, Page: 10, Move: 11,
	Row: 12, Sort: 13, Cell: 14, Resize: 15, Action: 16, Search: 17, Find: 18,
});

// What a field is for (ibProtocol::ibForm::ibProperty::ibFieldView) — a meaning; the widget is this page's.
export const FieldView = Object.freeze({ Input: 0, Flag: 1, Label: 2, Switch: 3, Gauge: 4, Slider: 5 });
// What a table column's cells are for (ibProtocol::ibForm::ibProperty::ibColumnView) — a field's first three, by its numbers.
export const ColumnView = Object.freeze({ Input: 0, Flag: 1, Label: 2 });

/** @type {Map<string, string>} a node type → its element's tag */
const tags = new Map();

// A control's Stretch in the sizer it stands in (wxStretch): Expand fills it across, Shrink keeps the control's own size.
const expand = 0x2000;

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
	// THE BACKGROUND ITS KIND IS GIVEN WHEN ITS AUTHOR PICKS NONE (a kind of its own says its own): the light theme's —
	// what an author's ink alone was chosen against — and the theme's for it, what a control leaving it empty is painted.
	// A control with none of its own stands on the surface under it (''), as the thin client's (Max, 2026-11-24: "an empty
	// background - each with its own"); the light side the style's form background (core/style/style.cpp).
	/** @type {[string, string]} */
	static platformBackground = ['rgb(238,242,246)', ''];

	constructor() {
		super();
		/** @type {ProtocolNode} the node it was last brought up to, without its children */
		this.node = {};
		/** @type {FormContext} */
		this.context = /** @type {any} */ (null);
		/** @type {HTMLElement} where its colours go — the field, not the row with the caption beside it */
		this.lookTarget = this;
		/** @type {Promise<unknown>} its first drawing, done — a table's rows and a sheet's cells come after the frame */
		this.drawing = Promise.resolve();
	}

	/** The first drawing. @param {ProtocolNode} node */
	build(node) {}

	/**
	 * Its first drawing done, and that of every control under it, each with the components it is drawn with come and drawn
	 * too (a component is loaded the first time it is met, and draws itself after that): what the form shows is what it
	 * will go on showing.
	 */
	async settled() {
		const controls = [this, ...this.querySelectorAll('*')].filter((element) => element instanceof Control);
		await Promise.all(controls.map((control) => /** @type {Control} */ (control).drawing));
		await componentsDrawn(this);
	}

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
	 * client's question of a control with a title beside it (ibControl::GetTitleExtent).
	 * @returns {number | null}
	 */
	calculateLabelSize() {
		return null;
	}

	/**
	 * The column's width, given to its label (ibControl::SetTitleWidth).
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
	 * Applied to every control on every pass: shown and enabled, the tooltip, the colours, the font, the sizes, its place
	 * in the sizer it stands in. Everything it sets it also clears, so what went away from the node goes away here too.
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

		const [text, background] = themedPair(node.ForegroundColour, node.BackgroundColour,
			/** @type {typeof Control} */ (this.constructor).platformBackground);
		this.lookTarget.style.color = text;
		this.lookTarget.style.backgroundColor = background;
		applyFont(this, node.Font);

		// A stored minimum raises a floor; it does not lower one — max() keeps the stylesheet's own floor too.
		const minimum = readSize(node.MinimumSize);
		const maximum = readSize(node.MaximumSize);
		this.style.minWidth = minimum.width > 0 ? `max(${minimum.width}px, var(--oes-min-width, 0px))` : '';
		this.style.minHeight = minimum.height > 0 ? `max(${minimum.height}px, var(--oes-min-height, 0px))` : '';
		this.style.maxWidth = maximum.width > 0 ? `${maximum.width}px` : '';
		this.style.maxHeight = maximum.height > 0 ? `${maximum.height}px` : '';

		// ITS PLACE IN THE SIZER IT STANDS IN, as its own node says (the server's ibSizerItem): its share of the free space,
		// whether it stretches across — in a grid's cell the other axis too, Shrink keeping its own width at the cell's
		// start, as the desktop's grid places it — the border round it. Said, not set: what lays it out reads it
		// (styles/form.css), and a control standing alone, a document in a tab of its own, does not. A node with none (a
		// page, a column) says none.
		const placed = node.Proportion !== undefined;
		const expanded = ((node.Stretch ?? 0) & expand) !== 0;
		const border = node.BorderSize ?? 0;
		const side = (/** @type {boolean | undefined} */ kept) => (kept && border > 0 ? `${border}px` : '0');
		const place = {
			'--oes-place-grow': String(node.Proportion ?? 0),
			'--oes-place-align': expanded ? 'stretch' : 'flex-start',
			'--oes-place-justify': expanded ? 'stretch' : 'start',
			'--oes-place-margin': `${side(node.BorderTop)} ${side(node.BorderRight)} ${side(node.BorderBottom)} ${side(node.BorderLeft)}`,
		};
		for (const [name, value] of Object.entries(place)) {
			if (placed)
				this.style.setProperty(name, value);
			else
				this.style.removeProperty(name);
		}
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
 * The components under an element come and drawn: one is loaded the first time it is met, and draws itself after that.
 * @param {Element} element
 */
export async function componentsDrawn(element) {
	const components = [...element.querySelectorAll('*')].filter((one) => one.localName.startsWith('wa-'));
	await Promise.all([...new Set(components.map((one) => one.localName))].map((tag) => customElements.whenDefined(tag)));
	await Promise.all(components.map((one) => /** @type {any} */ (one).updateComplete));
}

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

// THE PLATFORM'S OWN INK, in the light theme: what an author's fill alone was chosen against (the style's form text,
// core/style/style.cpp).
const defaultText = 'rgb(63,92,119)';

/**
 * A control's two colours as they are painted, as the form writes them: empty — the platform's, its author picked none
 * (Max, 2026-11-24); a kind with no colours (a sizer) names neither and is left to the stylesheet. Both the platform's:
 * the theme's (styles/tokens.css). One of them the author's: the author chose it against the light theme's other side,
 * so that side is what it gets - the light theme's ink, or its kind's ground - and the pair reads as the author saw it;
 * in the dark theme the pair is carried there whole (themed).
 * @param {string | undefined} foreground @param {string | undefined} background
 * @param {[string, string]} platform its kind's background in the light theme, and the theme's for it
 * (Control.platformBackground)
 * @returns {[string, string]}
 */
function themedPair(foreground, background, [platformBackground, themedBackground]) {
	if (foreground === undefined && background === undefined)
		return ['', ''];
	const text = colour(foreground);
	const fill = colour(background);
	if (text === '' && fill === '')
		return ['var(--oes-form-text)', themedBackground];
	return [themed(text || defaultText), themed(fill || platformBackground)];
}

/**
 * A cell's text colour and fill, either perhaps unnamed, as each theme paints them (themed): a fill named alone is read
 * with the theme's ink, which stands on it in either theme once the fill is carried. Ink named alone stays alone: the
 * fill under it is the row's - the current row, the current cell - and papering it would break them. (A sheet's cells
 * need none of it: its paper is the same in every theme.)
 * @param {string} text @param {string} fill
 * @returns {[string, string]}
 */
export function onFill(text, fill) {
	return [themed(text), themed(fill)];
}

/**
 * An author's colour, chosen in the light theme, as each theme paints it: itself in the light, carried in the dark
 * (inDark) - `light-dark()` follows the theme in force, so another chosen repaints it with nothing read again. '' stays.
 * @param {string} rgb `rgb(r,g,b)`, or ''
 */
function themed(rgb) {
	const channels = /^rgb\((\d+),(\d+),(\d+)\)$/.exec(rgb);
	return channels ? `light-dark(${rgb}, rgb(${inDark(channels.slice(1).map(Number)).join(',')}))` : rgb;
}

/**
 * An author's colour as the dark theme paints it - the thin client's sums (ibStyle::Get of a colour, core/style/style.cpp):
 * its lightness carried in OKLab from the light theme's form ground and ink onto the dark's (past the ink, the rest of
 * the way to black onto the rest of it to white), its hue kept, its chroma cut to what sRGB holds.
 * @param {number[]} rgb @returns {number[]}
 */
function inDark(rgb) {
	const [l, a, b] = okLabOf(rgb);
	const lightness = Math.min(Math.max(l < lightInk ? darkInk + (lightInk - l) / lightInk * (1 - darkInk)
		: darkGround + (l - lightGround) * (darkInk - darkGround) / (lightInk - lightGround), 0), 1);
	let held = 1;
	if (!inGamut([lightness, a, b])) {
		held = 0;
		let lost = 1;
		for (let step = 0; step < 24; step++) {
			const tried = (held + lost) / 2;
			if (inGamut([lightness, a * tried, b * tried]))
				held = tried;
			else
				lost = tried;
		}
	}
	return linearRgbOf([lightness, a * held, b * held]).map(channelOf);
}

/** A channel, 0..255, as linear light. @param {number} channel */
function linearOf(channel) {
	const c = channel / 255;
	return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

/** …and linear light, held to 0..1, as a channel. @param {number} linear */
function channelOf(linear) {
	const x = Math.min(Math.max(linear, 0), 1);
	return Math.round(Math.min(Math.max(x <= 0.0031308 ? 12.92 * x : 1.055 * x ** (1 / 2.4) - 0.055, 0), 1) * 255);
}

/** OKLab (Björn Ottosson): the lightness, 0..1, and two opposing axes. @param {number[]} rgb @returns {number[]} */
function okLabOf(rgb) {
	const [r, g, b] = rgb.map(linearOf);
	const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
	const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
	const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
	return [0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
		1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
		0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s];
}

/** …and back, as linear sRGB - a channel out of 0..1 where sRGB does not hold it. @param {number[]} lab */
function linearRgbOf([lightness, a, b]) {
	const l = (lightness + 0.3963377774 * a + 0.2158037573 * b) ** 3;
	const m = (lightness - 0.1055613458 * a - 0.0638541728 * b) ** 3;
	const s = (lightness - 0.0894841775 * a - 1.2914855480 * b) ** 3;
	return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
		-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
		-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s];
}

/** @param {number[]} lab */
function inGamut(lab) {
	return linearRgbOf(lab).every((channel) => channel >= -1e-7 && channel <= 1 + 1e-7);
}

// THE PLATFORM STYLE'S FORM GROUND AND INK, light and dark (core/style/style.cpp; styles/tokens.css --oes-form-bg and
// --oes-form-text) - their lightness, what an author's colour is carried between (inDark).
const [lightGround, lightInk, darkGround, darkInk] = [[238, 242, 246], [63, 92, 119], [34, 38, 43], [201, 214, 226]]
	.map((rgb) => okLabOf(rgb)[0]);

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
export function readSize(text) {
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
export function readFont(text) {
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

/** @type {CanvasRenderingContext2D | null} */
let measure = null;

/**
 * A text's width in a font, as the desktop's GetTextExtent gives it — read from the font, not from a layout, so a
 * caption on a page not on show is measured too.
 * @param {string} text
 * @param {CSSStyleDeclaration} style
 */
export function textWidth(text, style) {
	measure ??= /** @type {CanvasRenderingContext2D} */ (document.createElement('canvas').getContext('2d'));
	measure.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
	return Math.ceil(measure.measureText(text).width);
}
