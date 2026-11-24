// A FIELD — one control, drawn with the widget its View is (docs/private/form-field.md): typed into, on or off, shown,
// one of its items picked. The caption beside it and its side are the field's, whatever the view; the widget is its
// child's (FieldViewProvider), made for the view the node names. A new View is a new field: update answers false, and the
// form draws it anew — what was being typed goes with the old one, uncommitted.

import { Control, EventKind, FieldView, colour, defineControl, textWidth } from './control.js';
import { InputMask } from './inputMask.js';
import { TextEditor } from './textEditor.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// Where its caption stands (ibProtocol::ibForm::ibProperty::ibTitleLocation); None — no caption (its Caption is empty).
const TitleRight = 2;
const TitleNone = 3;
// Which way a switch, a gauge, a slider runs (ibProtocol::ibForm::ibProperty::Orient, wx's numbers): wxVERTICAL — one
// under another, upwards; else wxHORIZONTAL (4), side by side, the default.
const OrientVertical = 8;

// THE VIEW A FIELD IS DRAWN WITH — the server's ibFieldViewProvider, this page's half: its widget, put in the field after
// the caption; what it does with the person's input, sent as the field's event.
class FieldViewProvider {
	/** @param {Field} control the field it draws */
	constructor(control) {
		this.control = control;
		/** a well of its own — a field typed into; one without stands on the form (Field.applyCommon) */
		this.well = false;
		/** the field is drawn anew in its place — what it still says is not the person's (Field.update) */
		this.replaced = false;
	}

	/** Its widget made, and put in the field. @param {ProtocolNode} node */
	build(node) {}

	/** @param {ProtocolNode} node */
	update(node) {}

	/**
	 * Whether what it was built with is what the node says — false: the field is drawn anew.
	 * @param {ProtocolNode} node
	 * @param {ProtocolNode} previous
	 */
	sameBuild(node, previous) {
		return true;
	}

	/** Whether the caption is its text itself — a label bound to nothing: no title beside it. @param {ProtocolNode} node */
	captionIsText(node) {
		return false;
	}

	/** @param {number} kind @param {object} [args] */
	send(kind, args) {
		return this.control.send(kind, args);
	}
}

// ⭐ INPUT — typed into: the text editor a table's cell has too (textEditor.js), with the buttons its node was saved
// with. The text is committed on change and on Enter — event Change {Text} — never by the key; typing begun is the
// state event Input. Typing wins over the server: what the field shows is written from the node while nobody is in it,
// or while it still shows what the page last wrote there and no older commit of it is unanswered; otherwise the
// server's text waits and lands when the field is left with nothing changed.
class FieldViewInput extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.well = true;
		const multiline = node.MultilineMode === true;
		this.editor = new TextEditor({ multiline, password: node.PasswordMode === true });
		this.input = this.editor.field;
		this.control.lookTarget = this.input;
		this.control.append(this.input);

		/** what the page last wrote into the field from a node — while it holds anything else, it is being typed in */
		this.applied = '';
		/** the server's Text — what the field shows at rest */
		this.text = '';
		/** …and its EditText: the text the mask is typed over, where Text is not */
		this.editText = '';
		/** @type {string | undefined} the server's text, waiting for the field to be left */
		this.waiting = undefined;
		/** commits sent and not yet answered */
		this.commits = 0;
		this.typing = false;

		this.input.addEventListener('input', () => {
			if (!this.typing) {
				this.typing = true;
				this.send(EventKind.Input);
			}
		});
		this.input.addEventListener('change', () => this.commit());
		if (!multiline) {
			this.input.addEventListener('keydown', (/** @type {KeyboardEvent} */ key) => {
				if (key.key === 'Enter') {
					key.preventDefault();
					this.commit();
				}
			});
		}
		// A text the server sent while the field was typed in lands when the field is left with no net change. A masked
		// field left untouched shows its text at rest again.
		this.input.addEventListener('blur', () => {
			if (this.input.value === this.applied
				&& (this.waiting !== undefined || (!this.editor.mask.isEmpty() && this.commits === 0))) {
				const text = this.shownText(false);
				this.input.value = text;
				this.applied = text;
			}
			this.waiting = undefined;
		});
		// Coming into a masked field shows the mask: the text it is typed over, laid into it, all of it selected.
		this.input.addEventListener('focus', () => {
			if (this.editor.mask.isEmpty() || this.input.value !== this.applied)
				return;
			const text = this.shownText(true);
			this.applied = text;
			this.editor.changeValue(text, 0, text.length);
		});

		this.editor.onButton = async (kind) => {
			// The button acts on the field as it stands: text typed and not committed is let go.
			this.applied = this.input.value;
			this.waiting = undefined;
			this.typing = false;
			await this.send(kind);
			if (kind === EventKind.Clear) {
				this.input.focus({ preventScroll: true });
				this.input.setSelectionRange?.(0, 0);
			}
		};
	}

	async commit() {
		// A field drawn anew — its view changed under the person's hands — lets what was typed in the old one go: the
		// change its editor fires as it loses the focus on the way off the page is not the person's (the thin client's
		// IsBeingDeleted).
		if (this.replaced)
			return;
		const sent = this.input.value ?? '';
		this.applied = sent;
		this.waiting = undefined;
		this.typing = false;
		this.commits++;
		try {
			await this.send(EventKind.Change, { Text: sent });
		}
		finally {
			this.commits--;
		}
	}

	/** @param {ProtocolNode} node @param {ProtocolNode} previous */
	sameBuild(node, previous) {
		return (node.MultilineMode === true) === (previous.MultilineMode === true)
			&& (node.PasswordMode === true) === (previous.PasswordMode === true);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		// Typed into where it may be written and its text edit mode is on; off, its value is still picked and cleared — the
		// buttons stay (the thin client's).
		this.input.toggleAttribute('readonly', state.ReadOnly === true || node.TexteditMode === false);
		this.editor.showButtons(node);
		this.editor.setMask(InputMask.parse(state.Mask ?? ''));

		this.text = state.Text ?? '';
		this.editText = state.EditText ?? '';
		const focused = this.control.contains(document.activeElement) || this.input.matches(':focus-within');
		const text = this.shownText(focused);
		if (!focused || (this.input.value === this.applied && this.commits <= 1)) {
			if (this.input.value !== text) {
				// The server's reformatted text lands under the caret, not after it.
				const native = this.input.input ?? this.input.textarea;
				const start = focused ? native?.selectionStart : null;
				const end = focused ? native?.selectionEnd : null;
				this.input.value = text;
				if (start !== null && start !== undefined)
					this.input.setSelectionRange?.(Math.min(start, text.length), Math.min(end ?? start, text.length));
			}
			this.applied = text;
			this.waiting = undefined;
		}
		else
			this.waiting = text;
	}

	/**
	 * The text the field shows while nobody types into it: the server's — and under a mask, where the person stands in
	 * the field, the text the mask is typed over, laid into it; elsewhere an empty text is the mask's skeleton.
	 * @param {boolean} focused
	 */
	shownText(focused) {
		const mask = this.editor.mask;
		if (mask.isEmpty())
			return this.text;
		if (focused)
			return mask.apply(this.editText !== '' ? this.editText : this.text);
		return this.text === '' ? mask.apply('') : this.text;
	}
}

// ⭐ FLAG — a box, toggled back as event Toggle {Checked}; read-only, it shows its mark and takes no click.
class FieldViewFlag extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.box = /** @type {any} */ (document.createElement('wa-checkbox'));
		this.box.setAttribute('size', 's');
		this.box.addEventListener('change', () => this.send(EventKind.Toggle, { Checked: this.box.checked === true }));
		this.control.lookTarget = this.box;
		this.control.append(this.box);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		this.box.checked = state.Checked === true;
		this.box.toggleAttribute('disabled', state.ReadOnly === true);
	}
}

// ⭐ LABEL — shown: bound, the value beside the caption, a link pressed back as event Open; bound to nothing — a
// heading, a note — the caption is the text itself.
class FieldViewLabel extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.text = document.createElement('span');
		this.text.className = 'text';
		this.text.addEventListener('click', () => {
			if (this.control.state(this.control.node).Link === true)
				this.send(EventKind.Open);
		});
		this.control.lookTarget = this.text;
		this.control.append(this.text);
	}

	/** @param {ProtocolNode} node */
	captionIsText(node) {
		return this.control.state(node).Text === undefined;
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		this.text.textContent = state.Text ?? state.Caption ?? node.Title ?? '';
		this.text.classList.toggle('link', state.Link === true);
	}
}

// ⭐ SWITCH — its items (the State's Choices, each its Caption, Selected on the one the value stands on) as radios in a
// row or a column (Orient); one picked is event Change {Member: its place}. Read-only, it takes no pick.
class FieldViewSwitch extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.group = /** @type {any} */ (document.createElement('wa-radio-group'));
		this.group.setAttribute('size', 's');
		this.group.addEventListener('change', () => {
			const place = Number(this.group.value);
			if (Number.isInteger(place) && place >= 0)
				this.send(EventKind.Change, { Member: place });
		});
		this.control.lookTarget = this.group;
		this.control.append(this.group);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		/** @type {ProtocolNode[]} */
		const items = state.Choices?.NodeChildren ?? [];

		// A radio an item, as many as the State gives — each its place as its value.
		while (this.group.children.length > items.length)
			this.group.lastElementChild.remove();
		while (this.group.children.length < items.length) {
			const radio = document.createElement('wa-radio');
			radio.setAttribute('value', String(this.group.children.length));
			this.group.append(radio);
		}
		items.forEach((item, place) => {
			const radio = this.group.children[place];
			const caption = item.Caption ?? '';
			if (radio.textContent !== caption)
				radio.textContent = caption;
		});

		const selected = items.findIndex((item) => item.Selected === true);
		this.group.value = selected >= 0 ? String(selected) : null;
		this.group.orientation = node.Orient === OrientVertical ? 'vertical' : 'horizontal';
		this.group.toggleAttribute('disabled', state.ReadOnly === true);
	}
}

// ⭐ GAUGE — a track filled to the State's Position of the field's Range, along its Orient; it sends nothing.
class FieldViewGauge extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.track = document.createElement('span');
		this.track.className = 'gauge';
		this.fill = document.createElement('span');
		this.fill.className = 'fill';
		this.track.append(this.fill);
		this.control.lookTarget = this.track;
		this.control.append(this.track);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		const range = node.Range > 0 ? node.Range : 100;
		const share = Math.max(0, Math.min(100, ((state.Position ?? 0) / range) * 100));
		this.track.classList.toggle('vertical', node.Orient === OrientVertical);
		this.fill.style.setProperty('--oes-gauge-share', `${share}%`);
	}
}

// ⭐ SLIDER — a thumb at the State's Position between the field's MinValue and MaxValue, along its Orient; let go, it is
// event Change {Position}. Read-only, it moves nothing.
class FieldViewSlider extends FieldViewProvider {
	/** @param {ProtocolNode} node */
	build(node) {
		this.slider = /** @type {any} */ (document.createElement('wa-slider'));
		this.slider.setAttribute('size', 's');
		// `change` is the value let go — not the `input` of a thumb still dragged; a value set from here sends neither.
		this.slider.addEventListener('change', () => this.send(EventKind.Change, { Position: Math.round(Number(this.slider.value)) }));
		this.control.lookTarget = this.slider;
		this.control.append(this.slider);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.control.state(node);
		this.slider.min = node.MinValue ?? 0;
		this.slider.max = node.MaxValue ?? 100;
		// The value — none in the designer.
		if (state.Position !== undefined)
			this.slider.value = state.Position;
		const vertical = node.Orient === OrientVertical;
		this.slider.orientation = vertical ? 'vertical' : 'horizontal';
		this.slider.classList.toggle('vertical', vertical);
		this.slider.readonly = state.ReadOnly === true;
	}
}

/**
 * The child of a view; an Input for one this page does not know.
 * @param {number} view
 * @param {Field} control
 * @returns {FieldViewProvider}
 */
function makeProvider(view, control) {
	switch (view) {
	case FieldView.Flag: return new FieldViewFlag(control);
	case FieldView.Label: return new FieldViewLabel(control);
	case FieldView.Switch: return new FieldViewSwitch(control);
	case FieldView.Gauge: return new FieldViewGauge(control);
	case FieldView.Slider: return new FieldViewSlider(control);
	default: return new FieldViewInput(control);
	}
}

class Field extends Control {
	// A field's own, whatever its view: white — the theme's, as a table's. A view with no well stands on the form
	// instead (applyCommon).
	/** @type {[string, string]} */
	static platformBackground = ['rgb(255,255,255)', 'var(--oes-table-bg)'];

	/** @param {ProtocolNode} node */
	build(node) {
		this.caption = document.createElement('span');
		this.caption.className = 'caption';
		this.append(this.caption);
		/** the label column's width it was given — a field with no caption keeps the column too */
		this.labelWidth = 0;

		this.view = node.View ?? FieldView.Input;
		this.provider = makeProvider(this.view, this);
		this.provider.build(node);
		this.update(node, {});
	}

	/** Whether its caption takes part in the label column: on the left — not on the right, not none — and not its text
	 * itself. */
	takesColumn() {
		return this.node.TitleLocation !== TitleRight && this.node.TitleLocation !== TitleNone
			&& !this.provider.captionIsText(this.node);
	}

	/** Its caption's width in its font; an empty caption is 0 and still takes part, as the desktop's does. */
	calculateLabelSize() {
		if (!this.takesColumn())
			return null;
		const text = this.caption.textContent ?? '';
		return text === '' ? 0 : textWidth(text, getComputedStyle(this.caption));
	}

	/** @param {number} width */
	applyLabelSize(width) {
		this.labelWidth = this.takesColumn() ? width : 0;
		this.style.setProperty('--oes-caption-width', this.labelWidth > 0 ? `${this.labelWidth}px` : '');
		this.caption.hidden = this.caption.textContent === '' && this.labelWidth <= 0;
	}

	/**
	 * @param {ProtocolNode} node
	 * @param {ProtocolNode} previous
	 */
	update(node, previous) {
		// The view is what it is built with — and what the view's widget is built with, the view's to say. Drawn anew, it
		// is told so first: the browser blurs a field as it is taken off the page, before it is off.
		if ((node.View ?? FieldView.Input) !== this.view
			|| (previous.NodeType !== undefined && !this.provider.sameBuild(node, previous))) {
			this.provider.replaced = true;
			return false;
		}

		const state = this.state(node);
		this.caption.textContent = this.provider.captionIsText(node) ? '' : (state.Caption ?? node.Title ?? '');
		this.caption.hidden = this.caption.textContent === '' && this.labelWidth <= 0;
		this.classList.toggle('title-right', node.TitleLocation === TitleRight);
		this.provider.update(node);
		return true;
	}

	/** @param {ProtocolNode} node */
	applyCommon(node) {
		super.applyCommon(node);
		// A view with no well (a flag, a label, a switch, a gauge, a slider) stands on the form, as the thin client leaves
		// its kind's background to the theme: the field's own background is no ground of it — an author's colour is.
		if (!this.provider.well && colour(node.BackgroundColour) === '')
			this.lookTarget.style.backgroundColor = '';
	}
}

defineControl('Field', Field);
