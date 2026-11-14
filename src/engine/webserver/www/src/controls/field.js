// A FIELD (Field) — one value of a source, drawn as its Kind says (ibProtocol's ibFieldKind): an Input field is the
// text field (Textctrl), the same properties, State and events; Radio buttons are a button for each of the State's
// Choices, the Selected one checked. Which one is checked is the server's: a click asks for that choice — event Change
// {Choice}, its place among the Choices — and the next frame says what stands; a read-only field puts the click back.

import { Control, EventKind, defineControl, defineKind } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// A field's kinds (ibForm::ibProperty::ibFieldKind).
const FieldKind = Object.freeze({ Input: 0, RadioButtons: 1 });

class FieldRadioButtons extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.caption = document.createElement('span');
		this.caption.className = 'caption';
		this.choices = document.createElement('div');
		this.choices.className = 'choices';
		this.append(this.caption, this.choices);
		/** @type {string} the captions the buttons were built with — others build them anew */
		this.built = '';
		/** @type {HTMLInputElement[]} */
		this.marks = [];
		this.selected = -1;
		this.readOnly = false;
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		this.caption.textContent = state.Caption ?? '';
		this.caption.hidden = !state.Caption;

		const captions = (state.Choices?.NodeChildren ?? []).map((/** @type {ProtocolNode} */ choice) => String(choice.Caption ?? ''));
		const built = JSON.stringify(captions);
		if (built !== this.built) {
			this.built = built;
			this.marks = captions.map((caption, index) => {
				const label = document.createElement('label');
				const mark = document.createElement('input');
				mark.type = 'radio';
				mark.addEventListener('change', () => this.choose(index));
				const text = document.createElement('span');
				text.textContent = caption;
				label.append(mark, text);
				return mark;
			});
			this.choices.replaceChildren(...this.marks.map((mark) => /** @type {HTMLElement} */ (mark.parentElement)));
		}

		this.selected = typeof state.Selected === 'number' ? state.Selected : -1;
		this.readOnly = state.ReadOnly === true;
		this.toggleAttribute('readonly', this.readOnly);
		this.showSelected();
		return true;
	}

	/** The checks as the server said last. */
	showSelected() {
		this.marks.forEach((mark, index) => { mark.checked = index === this.selected; });
	}

	/** @param {number} index */
	choose(index) {
		this.showSelected();
		if (!this.readOnly)
			this.send(EventKind.Change, { Choice: index });
	}
}

defineControl('FieldRadioButtons', FieldRadioButtons);
defineKind('Field', FieldKind.Input, 'Textctrl');
defineKind('Field', FieldKind.RadioButtons, 'FieldRadioButtons');
