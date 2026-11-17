// THE FIELDS A SETTING IS MADE OF, as the server describes them (the thin client's ibSettingsFieldTree): each by its
// name, its presentation, its type, its picture; a field reads as its whole path (Counterparty.Code). A reference
// field opens on the fields of what it points at — asked of the server the first time (the window's act, Expand
// with its Targets), which asks again with them (Expanded, and their References). One tree a window holds, shown in
// a pane beside every editor that takes a field from it; a field opened, every pane and every editor drawn again.

/** @typedef {import('../protocol/pictures.js').Pictures} Pictures */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/**
 * A field offered — and, once a reference is opened, the fields of what it points at under it.
 * A field's `type` is its type whole, as the server described it — what a cell of several types puts back to the server
 * to be chosen by (the thin client's ChooseValue); `types` — the class ids in it.
 * @typedef {{name: string, path: string, presentation: string, title: string, id: number, picture: string,
 *   type: ProtocolNode | undefined, types: string[], children: Field[] | null, depth: number, open: boolean}} Field
 */

// The primitive types a value is typed in for (their class ids, as the wire names them).
export const Type = Object.freeze({
	Boolean: '134625395325936544', Number: '104754294576172486', Date: '95855587264049108', String: '125750747597698322',
});
// A date no one set, as the wire reads it (packet.js, dateText) — and takes it back, as the calendar sends one.
export const emptyDate = '0001-01-01 00:00:00';

export class SettingsFieldTree {
	/**
	 * @param {Pictures} pictures
	 * @param {ProtocolNode} request the window's question: its Fields and References
	 * @param {(field: Field, targets: number[]) => void} expand asks the server for a reference's fields
	 */
	constructor(pictures, request, expand) {
		this.pictures = pictures;
		this.expand = expand;
		/** @type {Map<string, ProtocolNode>} a reference type → what it points at (Targets) and its empty value */
		this.references = new Map();
		this.takeReferences(request.References);
		/** @type {Field[]} */
		this.fields = this.fieldsOf(request.Fields, null);
		/** @type {Field | null} the field taken in the panes */
		this.taken = null;
		/** @type {Field | null} the field being dragged out of a pane */
		this.dragged = null;
		/** @type {{pane: HTMLElement, pick: (field: Field) => void}[]} the panes, drawn again as a field opens */
		this.panes = [];
		/** @type {(() => void)[]} what is drawn again with them — the editors' lines, which name the fields */
		this.listeners = [];
	}

	/** @param {ProtocolNode | undefined} references */
	takeReferences(references) {
		for (const one of references?.NodeChildren ?? [])
			this.references.set(String(one.Type), one);
	}

	/**
	 * @param {ProtocolNode | undefined} fields as the server describes them
	 * @param {Field | null} parent
	 * @returns {Field[]}
	 */
	fieldsOf(fields, parent) {
		return (fields?.NodeChildren ?? []).filter((one) => one.Available !== false).map((one) => {
			const name = String(one.Name ?? '');
			const presentation = String(one.Presentation || name);
			return {
				name,
				path: parent !== null ? `${parent.path}.${name}` : name,
				// Its own line in the tree reads as itself — a presentation may hold a dot of its own («Unit, abbr.»).
				presentation,
				// A field reads as its whole path — which of two it is, a value reached through a reference shows.
				title: parent !== null ? `${parent.title}.${presentation}` : presentation,
				id: Number(one.Id ?? -1),
				picture: one.Picture,
				type: one.Type,
				types: (one.Type?.Types ?? []).map((/** @type {any} */ type) => String(type.TypeId)),
				children: null,
				depth: parent !== null ? parent.depth + 1 : 0,
				open: false,
			};
		});
	}

	/** @param {Field} field */
	isReference(field) {
		return field.types.some((type) => this.references.has(type));
	}

	/** Every field known — those opened with them — in the order shown. */
	allFields() {
		/** @type {Field[]} */
		const all = [];
		const walk = (/** @type {Field[]} */ fields) => {
			for (const field of fields) {
				all.push(field);
				if (field.children !== null)
					walk(field.children);
			}
		};
		walk(this.fields);
		return all;
	}

	/** @param {string} path */
	fieldAt(path) {
		return this.allFields().find((field) => field.path === path);
	}

	/**
	 * How a field reads, by its path: its title when it is known, else the text it was set with, else the path.
	 * @param {string} path
	 * @param {string} [text]
	 */
	titleOf(path, text = '') {
		return this.fieldAt(path)?.title ?? (text || path);
	}

	/**
	 * A type's own empty, packed — False, 0, an empty text, an empty date, the empty reference of the kind (the thin
	 * client's AdjustValue); none for a type it does not know.
	 * @param {string} type
	 * @returns {object | undefined}
	 */
	emptyOf(type) {
		if (this.references.has(type)) {
			const { Presentation: _, ...reference } = this.references.get(type)?.Value ?? {};
			return reference;
		}
		const empty = /** @type {Record<string, any>} */ ({
			[Type.Boolean]: false, [Type.Number]: 0, [Type.String]: '', [Type.Date]: emptyDate,
		})[type];
		return empty !== undefined ? { t: type, v: empty } : undefined;
	}

	/**
	 * A reference field opened on the fields of what it points at — asked of the server the first time — or closed.
	 * @param {Field} field
	 */
	open(field) {
		if (field.children !== null) {
			field.open = !field.open;
			this.draw();
			return;
		}
		this.expand(field, [...new Set(field.types.flatMap((type) => this.references.get(type)?.Targets ?? []))]);
	}

	/**
	 * The server's answer to Expand: the field's own fields, and what their references point at.
	 * @param {Field} field
	 * @param {ProtocolNode} request asked again with Expanded and References
	 */
	opened(field, request) {
		this.takeReferences(request.References);
		field.children = this.fieldsOf(request.Expanded, field);
		field.open = true;
		this.pictures.fetchFor(request).catch(() => {}).then(() => this.draw());
	}

	/**
	 * A pane of the fields, drawn now and with every change; a double click takes a field in.
	 * @param {(field: Field) => void} pick
	 */
	pane(pick) {
		const pane = document.createElement('div');
		pane.className = 'settings-field-list';
		this.panes.push({ pane, pick });
		this.drawPane(pane, pick);
		return pane;
	}

	/** @param {() => void} listener drawn again with the panes */
	listen(listener) {
		this.listeners.push(listener);
	}

	/** Every pane drawn again, and what listens. */
	draw() {
		for (const { pane, pick } of this.panes)
			this.drawPane(pane, pick);
		for (const listener of this.listeners)
			listener();
	}

	/**
	 * @param {HTMLElement} pane
	 * @param {(field: Field) => void} pick
	 */
	drawPane(pane, pick) {
		/** @type {HTMLElement[]} */
		const lines = [];
		const walk = (/** @type {Field[]} */ fields) => {
			for (const field of fields) {
				lines.push(this.line(field, pick));
				if (field.open && field.children !== null)
					walk(field.children);
			}
		};
		walk(this.fields);
		pane.replaceChildren(...lines);
	}

	/**
	 * @param {Field} field
	 * @param {(field: Field) => void} pick
	 */
	line(field, pick) {
		const line = document.createElement('div');
		line.className = 'settings-field';
		line.classList.toggle('taken', field === this.taken);
		line.style.paddingInlineStart = `${0.25 + field.depth * 1.125}rem`;
		const toggle = document.createElement('button');
		toggle.type = 'button';
		toggle.className = 'settings-field-toggle';
		const opens = this.isReference(field);
		toggle.textContent = opens ? (field.open ? '−' : '+') : '';
		toggle.disabled = !opens;
		toggle.addEventListener('click', (event) => {
			event.stopPropagation();
			this.open(field);
		});
		line.append(toggle);
		const url = this.pictures.url(field.picture);
		if (url) {
			const image = document.createElement('img');
			image.src = url;
			image.alt = '';
			line.append(image);
		}
		line.append(field.presentation);
		line.title = field.title;
		line.addEventListener('click', () => {
			this.taken = field;
			for (const { pane } of this.panes)
				for (const one of pane.children)
					one.classList.toggle('taken', /** @type {HTMLElement} */ (one).title === field.title);
		});
		line.addEventListener('dblclick', () => pick(field));
		// Dragged out onto a page's lines, it joins them as a double click does (the thin client's same-process drag:
		// what moved is what the tree remembers, the text carried is a stub).
		line.draggable = true;
		line.addEventListener('dragstart', (event) => {
			this.dragged = field;
			event.dataTransfer?.setData('text/plain', field.path);
		});
		line.addEventListener('dragend', () => { this.dragged = null; });
		return line;
	}
}
