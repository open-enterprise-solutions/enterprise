// THE APPLICATION'S PROPERTY CELLS — the classes a runtime session is sent (the form editor's rows, which the server cuts
// to what a person may arrange: ibValueFrame::GetAllowedUserProperty), and the plain ones beside them. Each reads the
// row's Value as the server stores it and writes a new one back in the same shape, as the thin client's cell does
// (frmclient/propertyManager/property/advprop): a flag, a text, a number, one of the choices; a colour `r,g,b`, a font
// `face,style,weight,pointSize,family,underlined`, a size `w,h` — empty for none, -1 for no size.

import { _, language } from '../localization.js';
import { defineProperty } from './index.js';
import { editTranslate, readingOf, translated } from './translate.js';
import { readFont, readSize } from '../controls/control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// A font a person begins from where the control has none: the family's face at the style's text size (core/style).
const textFontPoints = 9;
const fontFamilyDefault = 70;
const fontStyleNormal = 90;
const fontStyleItalic = 93;
const fontWeightNormal = 400;
const fontWeightBold = 700;

// A flag — checked or not (ibPG_BOOL_USE_CHECKBOX).
defineProperty('ibPropertyBoolean', (row, change) => {
	const box = document.createElement('input');
	box.type = 'checkbox';
	box.checked = row.Value === true;
	box.addEventListener('change', () => change(box.checked, box.checked ? 'True' : 'False'));
	return cell(box);
});

// A text — typed as it is.
defineProperty('ibPropertyString', (row, change) => cell(textField(String(row.Value ?? ''), (text) => change(text))));

// A translated text — the language in force typed (ibTStringProperty::StringToValue): the reading shown is the
// Presentation, the language the session reads such a text in gets the new one, the others kept (translate.js). Every
// language at once under its … (the thin client's translation window, DisplayEditorDialog — titled as the property).
defineProperty('ibPropertyTString', (row, change) => {
	const raw = String(row.Value ?? '');
	const reading = String(row.Presentation ?? '');
	const write = (/** @type {string} */ edited) => change(edited, readingOf(edited, language()));
	const open = document.createElement('button');
	open.type = 'button';
	open.textContent = '…';
	open.title = _('Languages');
	open.addEventListener('click', async () => {
		const edited = await editTranslate(String(row.Title || row.Name || ''), raw);
		if (edited !== null && edited !== raw)
			write(edited);
	});
	return cell(textField(reading, (text) => write(translated(raw, text))), open);
});

// A number — any, a whole one, one not below 0 (the thin client's one cell for the three).
for (const [className, step, min] of /** @type {[string, string, string][]} */ ([
	['ibPropertyNumber', 'any', ''], ['ibPropertyInteger', '1', ''], ['ibPropertyUInteger', '1', '0'],
])) {
	defineProperty(className, (row, change) => {
		const input = document.createElement('input');
		input.type = 'number';
		input.step = step;
		input.min = min;
		const held = Number(row.Value ?? 0);
		input.value = String(held);
		input.addEventListener('change', () => {
			const typed = input.valueAsNumber;
			if (Number.isFinite(typed) && input.checkValidity())
				change(typed, String(typed));
			else
				input.value = String(held);
		});
		return cell(input);
	});
}

// One of the choices the row brings (ibPGEnumProperty): their Ids, their Titles.
defineProperty('ibPropertyEnum', (row, change) => {
	const select = document.createElement('select');
	for (const choice of row.NodeChildren ?? []) {
		const option = document.createElement('option');
		option.value = String(choice.Id);
		option.textContent = choice.Title ?? String(choice.Id);
		select.append(option);
	}
	select.value = String(row.Value);
	select.addEventListener('change', () => change(Number(select.value), select.selectedOptions[0]?.textContent ?? ''));
	return cell(select);
});

// A colour — the system's picker, and none at all (the thin client's choice offers the default first): `r,g,b` or empty.
defineProperty('ibPropertyColour', (row, change) => {
	const held = String(row.Value ?? '');
	const parts = held.split(',').map(Number);
	const set = parts.length === 3 && parts.every((part) => Number.isInteger(part) && part >= 0 && part <= 255);
	const picker = document.createElement('input');
	picker.type = 'color';
	picker.value = set ? `#${parts.map((part) => part.toString(16).padStart(2, '0')).join('')}` : '#000000';
	const said = document.createElement('span');
	said.textContent = set ? held : _('Default');
	picker.addEventListener('change', () => {
		const value = [1, 3, 5].map((at) => parseInt(picker.value.slice(at, at + 2), 16)).join(',');
		change(value, value);
	});
	const element = cell(picker, said, clearButton(set, () => change('', '')));
	element.classList.toggle('unset', !set);
	return element;
});

// A font — said in the cell, chosen under its … as the thin client's opens its font dialog: the face, the size, bold,
// italic, underlined; and none at all. What the choice does not show of it (the family) goes back as it came.
defineProperty('ibPropertyFont', (row, change) => {
	const held = String(row.Value ?? '');
	const font = readFont(held);
	const family = font !== null ? held.split(',')[4] || String(fontFamilyDefault) : String(fontFamilyDefault);
	const said = document.createElement('span');
	said.textContent = font !== null ? fontText(held) : _('Default');
	const choice = /** @type {any} */ (document.createElement('div'));
	choice.className = 'font-choice';
	choice.popover = 'auto';
	const face = textField(font?.face ?? '', () => write());
	face.placeholder = _('Default');
	face.ariaLabel = _('Face Name');
	const size = document.createElement('input');
	size.type = 'number';
	size.min = '1';
	size.step = '1';
	size.className = 'font-size';
	size.ariaLabel = _('Size');
	size.value = font !== null && font.size > 0 ? String(font.size) : '';
	size.placeholder = String(textFontPoints);
	size.addEventListener('change', () => write());
	const bold = toggle('B', _('Bold'), 'bold', font !== null && font.weight >= fontWeightBold, () => write());
	const italic = toggle('I', _('Italic'), 'italic', font?.italic === true, () => write());
	const underlined = toggle('U', _('Underlined'), 'underlined', font?.underlined === true, () => write());
	choice.append(face, size, bold, italic, underlined);
	// Escape closes the choice, not the window it stands in.
	choice.addEventListener('keydown', (/** @type {KeyboardEvent} */ event) => {
		if (event.key !== 'Escape')
			return;
		event.preventDefault();
		event.stopPropagation();
		choice.hidePopover();
		open.focus();
	});
	// A font begun from none takes the style's text size; one whose size is cleared, the same.
	const write = () => {
		const points = Number.isFinite(size.valueAsNumber) && size.valueAsNumber > 0 ? Math.round(size.valueAsNumber) : textFontPoints;
		const weight = bold.ariaPressed === 'true' ? fontWeightBold : font !== null && font.weight < fontWeightBold ? font.weight : fontWeightNormal;
		const value = [face.value.replace(/,/g, ''), italic.ariaPressed === 'true' ? fontStyleItalic : fontStyleNormal, weight, points,
			family, underlined.ariaPressed === 'true' ? 1 : 0].join(',');
		said.textContent = fontText(value);
		change(value, value);
	};
	const open = document.createElement('button');
	open.type = 'button';
	open.textContent = '…';
	open.title = _('Font');
	// Under the cell, its right edge on the cell's.
	open.addEventListener('click', () => {
		choice.showPopover();
		const under = element.getBoundingClientRect();
		choice.style.left = `${Math.max(0, under.right - choice.offsetWidth)}px`;
		choice.style.top = `${under.bottom}px`;
		face.focus();
	});
	const element = cell(said, open, clearButton(font !== null, () => change('', '')), choice);
	element.classList.toggle('unset', font === null);
	return element;
});

// A size and a point — two numbers, -1 for none (ibDefaultSize, ibDefaultPosition): a box left empty.
for (const [className, first, second] of [['ibPropertySize', 'Width', 'Height'], ['ibPropertyPoint', 'X', 'Y']]) {
	defineProperty(className, (row, change) => {
		const { width, height } = readSize(row.Value);
		const inputs = [[width, first], [height, second]].map(([value, label]) => {
			const input = document.createElement('input');
			input.type = 'number';
			input.step = '1';
			input.placeholder = String(label);
			input.ariaLabel = String(label);
			input.value = Number(value) >= 0 ? String(value) : '';
			input.addEventListener('change', () => {
				const value = inputs.map((one) => Number.isFinite(one.valueAsNumber) ? Math.round(one.valueAsNumber) : -1).join(',');
				change(value, value);
			});
			return input;
		});
		return cell(...inputs);
	});
}

/**
 * A font as a person reads it: its face — none, the default — its size, and how it is drawn.
 * @param {string} text the font's text
 */
export function fontText(text) {
	const font = readFont(text);
	if (font === null)
		return _('Default');
	return [font.face || _('Default'), `${font.size}pt`, font.weight >= fontWeightBold && _('Bold'), font.italic && _('Italic'),
		font.underlined && _('Underlined')].filter(Boolean).join(', ');
}

/**
 * A cell's box, what edits it inside.
 * @param {...(HTMLElement | string)} parts
 */
function cell(...parts) {
	const element = document.createElement('div');
	element.append(...parts);
	return element;
}

/**
 * A text box that hands its text over when it is done with — left, or Enter pressed — and only when it changed; Escape
 * puts the text back.
 * @param {string} text
 * @param {(text: string) => void} take
 */
function textField(text, take) {
	const input = document.createElement('input');
	input.type = 'text';
	input.spellcheck = false;
	input.value = text;
	let taken = text;
	const done = () => {
		if (input.value === taken)
			return;
		taken = input.value;
		take(input.value);
	};
	input.addEventListener('change', done);
	input.addEventListener('keydown', (event) => {
		if (event.key === 'Enter')
			done();
		// Escape over a text being typed puts it back, and the dialog stays.
		else if (event.key === 'Escape' && input.value !== taken) {
			event.preventDefault();
			event.stopPropagation();
			input.value = taken;
		}
	});
	return input;
}

/**
 * A button pressed in or out.
 * @param {string} caption
 * @param {string} label
 * @param {string} look how its caption is drawn — the class it is styled by
 * @param {boolean} pressed
 * @param {() => void} changed
 */
function toggle(caption, label, look, pressed, changed) {
	const button = document.createElement('button');
	button.type = 'button';
	button.className = `toggle ${look}`;
	button.textContent = caption;
	button.title = label;
	button.ariaPressed = String(pressed);
	button.addEventListener('click', () => {
		button.ariaPressed = String(button.ariaPressed !== 'true');
		changed();
	});
	return button;
}

/**
 * The value taken back to none.
 * @param {boolean} enabled
 * @param {() => void} clear
 */
function clearButton(enabled, clear) {
	const button = document.createElement('button');
	button.type = 'button';
	button.className = 'clear';
	button.textContent = '×';
	button.title = _('Default');
	button.disabled = !enabled;
	button.addEventListener('click', clear);
	return button;
}
