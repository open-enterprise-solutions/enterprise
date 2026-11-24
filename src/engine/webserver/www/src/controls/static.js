// A STATIC — a decoration, drawn as its View is: a text, its caption; a line, across or down as its orientation says. No
// value, no link (a value shown is a field's Label view, field.js).

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a static shows (ibProtocol::ibForm::ibProperty::ibStaticView) — a meaning; the drawing is this page's.
const StaticView = Object.freeze({ Text: 0, Line: 1 });

// The view as the page's styles name it; one this page does not know is a text.
const viewNames = ['text', 'line'];

// A line's orientation (wxVERTICAL).
const vertical = 8;

class Static extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.view = node.View ?? StaticView.Text;
		this.setAttribute('view', viewNames[this.view] ?? 'text');
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		// The view is what it is built with.
		if ((node.View ?? StaticView.Text) !== this.view)
			return false;

		if (this.view === StaticView.Line)
			this.setAttribute('orient', node.Orient === vertical ? 'vertical' : 'horizontal');
		else
			this.textContent = this.state(node).Caption ?? '';
		return true;
	}
}

defineControl('Static', Static);
