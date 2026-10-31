// A NOTEBOOK — its pages behind tabs, at the top or the bottom as OrientPage says, or with no tabs at all: then it is
// a panel showing its active page alone. The page selected is the State's ActivePage (a page's control id); a page
// picked goes back as event Page {Page}. A page shows its Title and Picture on its tab; a hidden one is out of it.

import { Control, EventKind, defineControl, dispose, reconcile } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// Where the tabs are (ibNotebook::ibTabLocation).
const noTabs = 0;
const bottom = 8;
// A page's orientation (wxHORIZONTAL).
const horizontal = 4;

class Notebook extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.group = /** @type {any} */ (document.createElement('wa-tab-group'));
		/** set while the page shown is the server's doing, so it is not sent back as the person's */
		this.quiet = false;
		this.group.addEventListener('wa-tab-show', (/** @type {any} */ event) => {
			if (!this.quiet)
				this.send(EventKind.Page, { Page: Number(event.detail.name) });
		});
		this.append(this.group);
		/** @type {Map<string, {tab: HTMLElement, panel: HTMLElement, page: Control | null}>} */
		this.pages = new Map();
	}

	/** @param {ProtocolNode} node */
	update(node) {
		return true;
	}

	/**
	 * Its pages: a tab and a panel each, the page's controls inside the panel.
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		const orient = node.OrientPage ?? 1;
		this.group.setAttribute('placement', orient === bottom ? 'bottom' : 'top');
		this.classList.toggle('no-tabs', orient === noTabs);

		const pages = (node.NodeChildren ?? []).filter((page) => this.state(page).Visible !== false);
		const kept = new Set();
		for (const page of pages) {
			const name = String(page.ControlId);
			kept.add(name);
			let held = this.pages.get(name);
			if (held === undefined) {
				const tab = document.createElement('wa-tab');
				tab.slot = 'nav';
				tab.setAttribute('panel', name);
				const panel = document.createElement('wa-tab-panel');
				panel.setAttribute('name', name);
				held = { tab, panel, page: null };
				this.pages.set(name, held);
			}
			const state = this.state(page);
			const picture = state.PictureVisible !== false ? this.context.picture(state.Picture) : '';
			held.tab.replaceChildren();
			if (picture) {
				const image = document.createElement('img');
				image.src = picture;
				image.alt = '';
				held.tab.append(image);
			}
			held.tab.append(state.Title ?? '');
			held.page = reconcile(held.panel, held.page, page, this.context);
			this.group.append(held.tab, held.panel);
		}
		for (const [name, held] of this.pages) {
			if (!kept.has(name)) {
				held.tab.remove();
				dispose(held.panel);
				this.pages.delete(name);
			}
		}

		const active = String(this.state(node).ActivePage ?? 0);
		if (this.pages.has(active) && this.group.active !== active) {
			this.quiet = true;
			this.group.active = active;
			queueMicrotask(() => { this.quiet = false; });
		}
	}
}

/** A page: its controls, laid as a box is — a row or a column, as its Orient says. */
class NotebookPage extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.setAttribute('orient', node.Orient === horizontal ? 'horizontal' : 'vertical');
		return true;
	}
}

defineControl('Notebook', Notebook);
defineControl('NotebookPage', NotebookPage);
