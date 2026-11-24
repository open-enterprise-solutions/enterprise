// THE LAYOUT — how a form lays its controls out, one control whose View says how (docs/private/form-layout.md), as the
// desktop's sizers do: a Box (a row or a column), a Wrap (a row that wraps), a Grid, a Group (a box with a frame and a
// caption). Each control in one takes its place as its own node says — its share of the free space (Proportion),
// whether it stretches across (Stretch), the border around it (control.js, applyCommon). A grid's cells are all one
// size, as the desktop's are. A box laid as a row folds into a column on a narrow screen (styles/form.css): a form drawn
// for a desktop still reads on a phone. A new View is a new layout: update answers false, and the form draws it anew.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// How a layout lays out (ibProtocol::ibForm::ibProperty::ibLayoutView) — a meaning; the drawing is this page's.
const LayoutView = Object.freeze({ Box: 0, Wrap: 1, Grid: 2, Group: 3 });

// The view as the page's styles name it; one this page does not know is a box.
const viewNames = ['box', 'wrap', 'grid', 'group'];

// A box's orientation (wxHORIZONTAL, wxVERTICAL).
const horizontal = 4;

// The legends drawn so far — each one's id, which its group is named by to a reader of the screen.
let legends = 0;

class Layout extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.view = node.View ?? LayoutView.Box;
		this.setAttribute('view', viewNames[this.view] ?? 'box');
		if (this.view === LayoutView.Group) {
			this.legend = document.createElement('div');
			this.legend.className = 'legend';
			this.legend.id = `oes-legend-${++legends}`;
			this.setAttribute('role', 'group');
			this.setAttribute('aria-labelledby', this.legend.id);
			this.append(this.legend);
		}
		this.update(node, {});
	}

	/**
	 * @param {ProtocolNode} node
	 * @param {ProtocolNode} previous
	 */
	update(node, previous) {
		// The view is what it is built with.
		if ((node.View ?? LayoutView.Box) !== this.view)
			return false;

		if (this.view === LayoutView.Grid) {
			// Every column the same, a share of the width rather than its widest content - and never narrower than what
			// it holds (a 1fr track takes no less than its items' least width, styles/form.css): a form too narrow for
			// them all gives the wide one its width rather than drawing it over its neighbour, and scrolls. The rows
			// likewise. No Cols, and Rows given: as many columns as the items need in that many rows; Rows given, that
			// many rows, an empty one keeping its place - as the desktop's grid counts them.
			const cols = node.Cols ?? 0;
			const rows = node.Rows ?? 0;
			const count = (node.NodeChildren ?? []).length;
			const columns = cols > 0 ? cols : rows > 0 ? Math.ceil(count / rows) : 1;
			this.style.gridTemplateColumns = `repeat(${Math.max(1, columns)}, 1fr)`;
			this.style.gridTemplateRows = rows > 0 ? `repeat(${rows}, 1fr)` : '';
		}
		else
			this.setAttribute('orient', node.Orient === horizontal ? 'horizontal' : 'vertical');

		if (this.legend)
			this.legend.textContent = this.state(node).Title ?? node.Title ?? '';
		return true;
	}
}

defineControl('Layout', Layout);
