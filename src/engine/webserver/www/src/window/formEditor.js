// CHANGE FORM — the person arranges the form they are in, as the thin client's window (ibDialogFormEditor) over the
// server's question (FormEditor): the form's controls a tree — a sizer's item not shown, its control in its place — and
// the one taken shows in a property grid what the server lets a person arrange of it, its sizer item's with it (the
// question's Controls, the rows of each by its id); Up and Down move it among its siblings — its item, when it stands in
// one. What is changed is queued. Apply hands over the form as it stands (Act Apply, Controls: each control by its id,
// its place among its siblings, its rows' values); the server applies it, draws the form anew and asks again — the
// window stays, the queue empty. Save keeps the arrangement for the person — what is queued applied first: it means the
// form as it stands; Restore drops it, the default coming back on the next open — each said in a line beside them, a
// refusal in a box. OK applies and closes; Cancel, Escape and × close — answered with no Act.

import { _, labelText } from '../localization.js';
import { PropertyGrid } from '../properties/propertyGrid.js';

/** @typedef {import('./questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {{id: number, type: string, title: string, children: Arranged[]}} Arranged a control, where it stands now */

// What the window does (ibRequest::ibFormEditor::ibAct), and how a save or a reset went (ibFormSettingsResult).
const Act = Object.freeze({ Apply: 1, Save: 2, Reset: 3 });
const Result = Object.freeze({ Ok: 0, NoStorage: 1, NoAddress: 2, Refused: 3 });
const sizerItem = 'SizerItem';
// Closing, the act waited on done — OK pressed with something queued, or the window closed while an act was answered.
const close = 'close';

export class FormEditor {
	/**
	 * Over the form the person is in (Questions.formOf).
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		this.questions = questions;
		this.request = request;
		/** @type {Arranged} */
		this.arranged = arranged(questions.formOf() ?? {});
		/** @type {Map<number, ProtocolNode[]>} a control's id → its rows */
		this.rows = new Map();
		/** @type {Map<ProtocolNode, number>} a row → the id of the control it is of */
		this.holders = new Map();
		/** @type {Map<string, ProtocolNode>} the rows changed and not applied, by control and name — each as changed */
		this.queue = new Map();
		/** whether a control was moved since the form was applied */
		this.moved = false;
		/** @type {number | null} the act whose answer the window waits for — the server asks again when it is done */
		this.waiting = null;
		/** @type {number | typeof close | null} what follows an Apply when the server asks again — a Save, or closing */
		this.then = null;
		/** @type {number} the control taken */
		this.taken = this.arranged.id;
		/** @type {Set<number>} the controls folded in the tree */
		this.folded = new Set();
		this.grid = new PropertyGrid((row, changed) => this.changed(row, changed));
		this.dialog = this.build();
		// In the page before its tree is drawn: the tree's lines are unfolded once they stand in it (drawTree).
		document.body.append(this.dialog);
		this.ask(request);
		this.dialog.open = true;
	}

	/** The window, its parts empty. */
	build() {
		const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
		dialog.className = 'form-editor';
		dialog.label = _('Form editor');
		const tools = document.createElement('div');
		tools.className = 'form-editor-tools';
		this.up = this.button(`↑ ${_('Up control')}`, () => this.move(-1));
		this.down = this.button(`↓ ${_('Down control')}`, () => this.move(1));
		tools.append(this.up, this.down);
		this.tree = /** @type {any} */ (document.createElement('wa-tree'));
		this.tree.addEventListener('wa-selection-change', (/** @type {any} */ event) => {
			const chosen = event.detail.selection[0];
			if (chosen !== undefined)
				this.show(Number(chosen.dataset.control));
		});
		this.tree.addEventListener('wa-expand', (/** @type {Event} */ event) => this.folded.delete(controlOf(event)));
		this.tree.addEventListener('wa-collapse', (/** @type {Event} */ event) => this.folded.add(controlOf(event)));
		// The thin client's menu over a control: Alt+Up, Alt+Down.
		this.tree.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			if (!event.altKey || (event.key !== 'ArrowUp' && event.key !== 'ArrowDown'))
				return;
			event.preventDefault();
			this.move(event.key === 'ArrowUp' ? -1 : 1);
		});
		const main = document.createElement('div');
		main.className = 'form-editor-main';
		main.append(this.tree, this.grid.element);
		// Save and Restore stand apart from OK, Apply, Cancel: they are not about this window's edit but whether the
		// arrangement outlives it.
		const setting = document.createElement('div');
		setting.className = 'form-editor-setting';
		this.save = this.button(_('Save setting'), () => this.queued() ? this.applyThen(Act.Save) : this.act(Act.Save));
		this.reset = this.button(_('Restore default setting'), () => this.act(Act.Reset));
		this.status = document.createElement('span');
		this.status.className = 'form-editor-status';
		setting.append(this.save, this.reset, this.status);
		this.ok = this.button(labelText('&OK'), () => this.queued() ? this.applyThen(close) : this.close(), 'brand');
		this.apply = this.button(_('Apply'), () => this.act(Act.Apply));
		this.cancel = this.button(_('Cancel'), () => this.close());
		for (const button of [this.ok, this.apply, this.cancel])
			button.slot = 'footer';
		dialog.append(tools, main, setting, this.ok, this.apply, this.cancel);
		// Enter is OK's, but on another button — a cell takes its own.
		dialog.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
			if (event.key === 'Enter' && !event.composedPath().some((node) => node instanceof Element && node.localName === 'wa-button')) {
				event.preventDefault();
				this.ok.click();
			}
		});
		dialog.addEventListener('wa-hide', (/** @type {Event} */ event) => {
			if (event.target !== dialog || dialog.answered)
				return;
			// An act on its way is answered first: the window closes when the server asks again.
			if (this.waiting !== null) {
				event.preventDefault();
				this.then = close;
				return;
			}
			this.close();
		});
		return dialog;
	}

	/**
	 * Asked again — the form as it stands after an act, whether it has a setting, how a save or a reset went.
	 * @param {ProtocolNode} request
	 */
	ask(request) {
		this.request = request;
		const form = this.questions.formOf();
		if (form !== null)
			this.arranged = arranged(form);
		this.rows.clear();
		this.holders.clear();
		for (const control of request.Controls?.NodeChildren ?? []) {
			const rows = control.NodeChildren ?? [];
			this.rows.set(control.Control, rows);
			for (const row of rows)
				this.holders.set(row, control.Control);
		}
		/** @type {Map<string, string>} a control's class → its picture */
		this.pictures = new Map((request.Classes?.NodeChildren ?? []).map((one) => [one.Name, one.Picture]));
		const acted = this.waiting;
		this.waiting = null;
		if (acted === Act.Apply) {
			this.queue.clear();
			this.moved = false;
		}
		const then = this.then;
		this.then = null;
		if (then === close) {
			this.close();
			return;
		}
		if (then === Act.Save) {
			this.act(Act.Save);
			return;
		}
		if (acted === Act.Save || acted === Act.Reset)
			this.said(acted, request.Result);
		if (find(this.arranged, this.taken) === null)
			this.taken = this.arranged.id;
		this.drawTree();
		this.show(this.taken);
	}

	/**
	 * An act of the window's answered — the form as it stands with an Apply; the server asks again when it is done.
	 * @param {number} act
	 */
	act(act) {
		/** @type {{Act: number, Controls?: object}} */
		const response = { Act: act };
		if (act === Act.Apply)
			response.Controls = { NodeChildren: this.written() };
		this.waiting = act;
		this.enable();
		this.questions.session.call('respond', { Id: this.request.Id, Response: response }).catch(() => {
			this.waiting = null;
			this.then = null;
			this.enable();
		});
	}

	/**
	 * What is queued applied, then the act that needs it — a Save of the form as it stands, or closing.
	 * @param {number | typeof close} then
	 */
	applyThen(then) {
		this.then = then;
		this.act(Act.Apply);
	}

	/** The window closed — answered with no Act. */
	close() {
		this.questions.answer(this.dialog, this.request, {});
	}

	/** Whether there is anything to apply. */
	queued() {
		return this.queue.size > 0 || this.moved;
	}

	/**
	 * A row's cell edited — queued.
	 * @param {ProtocolNode} row
	 * @param {ProtocolNode} changed the row as changed
	 */
	changed(row, changed) {
		this.queue.set(keyOf(this.holders.get(row), row.Name), changed);
		this.enable();
	}

	/**
	 * The control taken, its rows and its sizer item's in the grid.
	 * @param {number} id
	 */
	show(id) {
		this.taken = id;
		const found = find(this.arranged, id);
		const rows = found === null ? [] : [...(this.rows.get(id) ?? []),
			...(found.parent?.type === sizerItem ? this.rows.get(found.parent.id) ?? [] : [])];
		/** @type {Map<ProtocolNode, ProtocolNode>} */
		const changed = new Map();
		for (const row of rows) {
			const queued = this.queue.get(keyOf(this.holders.get(row), row.Name));
			if (queued !== undefined)
				changed.set(row, queued);
		}
		this.grid.show(rows, changed);
		this.enable();
	}

	/**
	 * The control taken moved among its siblings — its sizer item, when it stands in one.
	 * @param {number} step
	 */
	move(step) {
		const place = this.placeOf(this.taken);
		const to = place === null ? -1 : place.at + step;
		if (place === null || to < 0 || to >= place.siblings.length)
			return;
		[place.siblings[place.at], place.siblings[to]] = [place.siblings[to], place.siblings[place.at]];
		this.moved = true;
		this.drawTree();
		this.enable();
	}

	/**
	 * Where a control moves: what stands for it among its parent's children — itself, or the sizer items it is in.
	 * @param {number} id
	 * @returns {{siblings: Arranged[], at: number} | null}
	 */
	placeOf(id) {
		let found = find(this.arranged, id);
		while (found !== null && found.parent?.type === sizerItem)
			found = find(this.arranged, found.parent.id);
		if (found === null || found.parent === null)
			return null;
		return { siblings: found.parent.children, at: found.parent.children.indexOf(found.item) };
	}

	/** The tree drawn anew from the form as it stands, the control taken taken in it, the lines folded before kept so. */
	drawTree() {
		const root = this.item(this.arranged);
		this.tree.replaceChildren(...(root !== null ? [root] : []));
		// Unfolded once they stand in the page: a line put in folds itself while the line it is in says it is folded, and
		// one unfolded a moment before has not said so yet (Web Awesome 3.14, tree-item connectedCallback).
		for (const item of /** @type {NodeListOf<any>} */ (this.tree.querySelectorAll('wa-tree-item')))
			item.expanded = !this.folded.has(Number(item.dataset.control));
	}

	/**
	 * A control's line in the tree, and its controls under it; a sizer item is its control's.
	 * @param {Arranged} control
	 * @returns {HTMLElement | null}
	 */
	item(control) {
		if (control.type === sizerItem)
			return control.children.length > 0 ? this.item(control.children[0]) : null;
		const item = /** @type {any} */ (document.createElement('wa-tree-item'));
		item.dataset.control = String(control.id);
		const url = this.questions.pictures.url(this.pictures?.get(control.type));
		if (url) {
			const image = document.createElement('img');
			image.src = url;
			image.alt = '';
			item.append(image);
		}
		item.append(control.title || '<empty caption>');
		for (const child of control.children) {
			const line = this.item(child);
			if (line !== null)
				item.append(line);
		}
		item.selected = control.id === this.taken;
		return item;
	}

	/** The buttons alive by what there is to do; none while an act is answered. */
	enable() {
		const waiting = this.waiting !== null;
		const queued = this.queued();
		// Apply and Save act on the queue — alive exactly while there is something in it; Restore while there is a setting.
		this.apply.disabled = waiting || !queued;
		this.save.disabled = waiting || !queued;
		this.reset.disabled = waiting || this.request.HasSetting !== true;
		this.ok.disabled = waiting;
		const place = this.placeOf(this.taken);
		this.up.disabled = waiting || place === null || place.at === 0;
		this.down.disabled = waiting || place === null || place.at === place.siblings.length - 1;
	}

	/**
	 * How a save or a reset went: said in the line beside them, a refusal in a box.
	 * @param {number} act
	 * @param {number | undefined} result
	 */
	said(act, result) {
		if (result === Result.Ok) {
			this.status.textContent = act === Act.Save ? _('Setting saved.')
				: _('Setting cleared - the default layout returns on the next open.');
			return;
		}
		const text = act === Act.Reset ? _('The form setting could not be cleared.')
			// Not a failure: a form built from its data has nothing to key a setting by, and trying again never helps.
			: result === Result.NoAddress ? _('This form is generated from its data and cannot keep a setting of its own.')
				: result === Result.NoStorage ? _('Settings are not available in this session.')
					: _('The form setting could not be saved.');
		this.questions.dialog(_('Change form'), text, [{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
	}

	/**
	 * The form as it stands: each control by its id — its place among its siblings, the form among none — with its rows'
	 * values, what is queued in place of what came (the thin client's WriteControl).
	 * @returns {ProtocolNode[]}
	 */
	written() {
		/** @type {ProtocolNode[]} */
		const controls = [];
		/** @param {Arranged} control @param {number} position */
		const write = (control, position) => {
			/** @type {ProtocolNode} */
			const node = { Control: control.id, Position: position };
			for (const row of this.rows.get(control.id) ?? [])
				node[row.Name] = (this.queue.get(keyOf(control.id, row.Name)) ?? row).Value;
			controls.push(node);
			control.children.forEach((child, at) => write(child, at));
		};
		write(this.arranged, -1);
		return controls;
	}

	/**
	 * @param {string} caption
	 * @param {() => void} press
	 * @param {string} [variant]
	 */
	button(caption, press, variant = 'neutral') {
		const button = /** @type {any} */ (document.createElement('wa-button'));
		button.setAttribute('variant', variant);
		button.setAttribute('size', 's');
		button.textContent = caption;
		button.addEventListener('click', press);
		return button;
	}
}

/**
 * A form's view as the window arranges it: each control, its class, what a person reads it as (the thin client's
 * GetControlTitle — its Caption, a form's or a page's Title, a table's name; none — its class), its controls.
 * @param {ProtocolNode} node
 * @returns {Arranged}
 */
function arranged(node) {
	const state = node.State ?? {};
	const type = String(node.NodeType ?? '');
	const title = type === 'Tablebox' ? `TableBox: ${node.Name ?? ''}` : state.Caption || state.Title || node.Caption || node.Title || type;
	return {
		id: node.ControlId,
		type,
		title,
		children: (node.NodeChildren ?? []).filter((child) => child.ControlId !== undefined).map(arranged),
	};
}

/**
 * A control in the arrangement, and the one it stands in.
 * @param {Arranged} control
 * @param {number} id
 * @param {Arranged | null} [parent]
 * @returns {{item: Arranged, parent: Arranged | null} | null}
 */
function find(control, id, parent = null) {
	if (control.id === id)
		return { item: control, parent };
	for (const child of control.children) {
		const found = find(child, id, control);
		if (found !== null)
			return found;
	}
	return null;
}

/**
 * A row's place in the queue.
 * @param {number | undefined} control
 * @param {string} name
 */
function keyOf(control, name) {
	return `${control}/${name}`;
}

/** The control of a tree line folded or unfolded. @param {Event} event */
function controlOf(event) {
	return Number(/** @type {HTMLElement} */ (event.target).dataset.control);
}
