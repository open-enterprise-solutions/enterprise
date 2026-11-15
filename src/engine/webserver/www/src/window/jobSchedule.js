// A SCHEDULE EDITED — the fourteen fields of a schedule in four tabs, as the thin client's window (ibDialogJobSchedule):
// General — repeat every N seconds, minutes, hours or days, and the validity's start and end (unchecked: unbounded);
// Daily — the time window (it wraps midnight when it starts later than it ends) and the time after which a run does not
// start; Weekly — the days (none checked: every day), every N weeks, which of the month's such days; Monthly — the days
// of the month, those counted from its end, the months, every N months. Under the tabs, the sentence the schedule reads
// as, while it is being made. The anchor of «every N» is not edited here, and goes back as it came.
//
// The sentence and the check of a schedule that can never run are the core's (ibJobScheduleRules::Describe, IsValid),
// called by the thin client's window where it runs. A page has no core: they are read here as the core reads them.
//
// The schedule travels in its stored form (ibJobScheduleDescriptionMemory): IntervalSeconds; StartMinute, EndMinute,
// StopAfterMinute — minutes from midnight, -1 unset; DaysOfWeek, DaysOfMonth, DaysOfMonthFromEnd, Months — bit masks,
// 0 any; WeekdayOrdinal, EveryNWeeks, EveryNMonths — 0 unused; PeriodAnchor, ActiveFrom, ActiveTo — dates as JSON writes
// them, `yyyy-mm-dd hh:mm:ss`, "" unbounded.

import { _, format } from '../localization.js';
import { tabGroup } from '../settings/settingsWindow.js';

/** @typedef {Record<string, any>} StoredSchedule */

// The words are said when drawn, in the person's language — the window's as the thin client's window says them, the
// sentence's short names as the core does (ibDateTime::WeekDayShortName, MonthShortName).
// The interval's units, in seconds (s_unitSeconds).
const unitSeconds = [1, 60, 3600, 86400];
const units = () => [_('seconds'), _('minutes'), _('hours'), _('days')];
// Monday first, as the mask's bits (ibJobWeekDay).
const anyDay = 0x7F;
const weekDays = () => [_('Monday'), _('Tuesday'), _('Wednesday'), _('Thursday'), _('Friday'), _('Saturday'),
	_('Sunday')];
const weekDaysShort = () => [_('Mon', 'weekday, short'), _('Tue', 'weekday, short'), _('Wed', 'weekday, short'),
	_('Thu', 'weekday, short'), _('Fri', 'weekday, short'), _('Sat', 'weekday, short'), _('Sun', 'weekday, short')];
const months = () => [_('January'), _('February'), _('March'), _('April'), _('May'), _('June'), _('July'), _('August'),
	_('September'), _('October'), _('November'), _('December')];
const monthsShort = () => [_('Jan', 'month, short'), _('Feb', 'month, short'), _('Mar', 'month, short'),
	_('Apr', 'month, short'), _('May', 'month, short'), _('Jun', 'month, short'), _('Jul', 'month, short'),
	_('Aug', 'month, short'), _('Sep', 'month, short'), _('Oct', 'month, short'), _('Nov', 'month, short'),
	_('Dec', 'month, short')];
// Which of the month's such days (ibJobOrdinal): the choice's sixth is the last, 255.
const ordinals = () => [_('any'), _('first'), _('second'), _('third'), _('fourth'), _('fifth'), _('last')];
const lastOrdinal = 255;
const monthDays = () => Array.from({ length: 31 }, (item, at) => String(at + 1));

/**
 * The window's content over a schedule, and the schedule as it is edited.
 * @param {StoredSchedule} stored
 * @returns {{element: HTMLElement, edited: () => StoredSchedule}}
 */
export function scheduleEditor(stored) {
	const intervalValue = numberInput(1, 100000);
	const intervalUnit = choice(units());
	const activeFromUse = checkBox();
	const activeFrom = dayInput();
	const activeToUse = checkBox();
	const activeTo = dayInput();
	const windowUse = checkBox();
	const windowStart = timeInput();
	const windowEnd = timeInput();
	const stopAfterUse = checkBox();
	const stopAfter = timeInput();
	const weekDayList = checkList(weekDays());
	const everyNWeeks = numberInput(1, 52);
	const weekdayOrdinal = choice(ordinals());
	const monthDayList = checkList(monthDays());
	const fromEndList = checkList([_('last day'),
		...Array.from({ length: 30 }, (item, at) => format(_('%i before last'), at + 1))]);
	const monthList = checkList(months());
	const everyNMonths = numberInput(1, 12);

	const general = page(
		line(caption(_('Repeat every:')), intervalValue, intervalUnit),
		box(_('Validity'), line(labelled(activeFromUse, _('Start date:')), activeFrom),
			line(labelled(activeToUse, _('End date:')), activeTo)));
	const daily = page(
		box(_('Time window'), labelled(windowUse, _('Run only inside a time window')),
			line(caption(_('From:')), windowStart, caption(_('to:')), windowEnd)),
		box(_('Do not start after'), line(labelled(stopAfterUse, _('Time:')), stopAfter)));
	const weekly = page(
		caption(_('Days of week (none checked = every day):')), weekDayList.element,
		line(caption(_('Every N weeks:')), everyNWeeks),
		line(caption(_('Occurrence in month:')), weekdayOrdinal));
	const columns = document.createElement('div');
	columns.className = 'job-schedule-columns';
	for (const [title, list] of /** @type {[string, ReturnType<typeof checkList>][]} */ ([
		[_('Days of month:'), monthDayList], [_('From the end:'), fromEndList], [_('Months:'), monthList]])) {
		const column = document.createElement('div');
		column.append(caption(title), list.element);
		columns.append(column);
	}
	const monthly = page(columns, line(caption(_('Every N months:')), everyNMonths));

	// What is put in (LoadFromSchedule): an untouched day mask — every day — shows as none checked; checking all seven
	// would say the same while looking like a restriction.
	const [value, unit] = splitInterval(Number(stored.IntervalSeconds ?? 3600));
	intervalValue.value = String(value);
	intervalUnit.selectedIndex = unit;
	putDay(activeFromUse, activeFrom, stored.ActiveFrom);
	putDay(activeToUse, activeTo, stored.ActiveTo);
	const start = Number(stored.StartMinute ?? -1);
	const end = Number(stored.EndMinute ?? -1);
	windowUse.checked = start >= 0 && end >= 0;
	windowStart.value = clockText(windowUse.checked ? start : 0);
	windowEnd.value = clockText(windowUse.checked ? end : 0);
	const stop = Number(stored.StopAfterMinute ?? -1);
	stopAfterUse.checked = stop >= 0;
	stopAfter.value = clockText(stop >= 0 ? stop : 0);
	const days = Number(stored.DaysOfWeek ?? anyDay);
	weekDayList.put(days === 0 || days === anyDay ? 0 : days);
	everyNWeeks.value = String(Math.max(1, Number(stored.EveryNWeeks ?? 0)));
	everyNMonths.value = String(Math.max(1, Number(stored.EveryNMonths ?? 0)));
	const ordinal = Number(stored.WeekdayOrdinal ?? 0);
	weekdayOrdinal.selectedIndex = ordinal === lastOrdinal ? 6 : ordinal >= 1 && ordinal <= 5 ? ordinal : 0;
	monthDayList.put(Number(stored.DaysOfMonth ?? 0));
	fromEndList.put(Number(stored.DaysOfMonthFromEnd ?? 0));
	monthList.put(Number(stored.Months ?? 0));

	/**
	 * What is taken out (ApplyToSchedule). «Every 1» is stored as unused: a period of one would be named in the
	 * sentence as a restriction that restricts nothing.
	 * @returns {StoredSchedule}
	 */
	const edited = () => {
		const weeks = Number(everyNWeeks.value);
		const everyMonths = Number(everyNMonths.value);
		const ordinalAt = weekdayOrdinal.selectedIndex;
		const weekMask = weekDayList.mask();
		return {
			...stored,
			IntervalSeconds: Number(intervalValue.value) * unitSeconds[Math.max(0, intervalUnit.selectedIndex)],
			ActiveFrom: activeFromUse.checked && activeFrom.value !== '' ? `${activeFrom.value} 00:00:00` : '',
			ActiveTo: activeToUse.checked && activeTo.value !== '' ? `${activeTo.value} 00:00:00` : '',
			StartMinute: windowUse.checked ? minutesOf(windowStart) : -1,
			EndMinute: windowUse.checked ? minutesOf(windowEnd) : -1,
			StopAfterMinute: stopAfterUse.checked ? minutesOf(stopAfter) : -1,
			DaysOfWeek: weekMask === 0 ? anyDay : weekMask & anyDay,
			DaysOfMonth: monthDayList.mask(),
			DaysOfMonthFromEnd: fromEndList.mask(),
			Months: monthList.mask(),
			WeekdayOrdinal: ordinalAt === 6 ? lastOrdinal : Math.max(0, ordinalAt),
			EveryNWeeks: weeks > 1 ? weeks : 0,
			EveryNMonths: everyMonths > 1 ? everyMonths : 0,
		};
	};

	const element = document.createElement('div');
	element.className = 'job-schedule';
	const sentence = document.createElement('div');
	sentence.className = 'job-schedule-sentence';
	element.append(tabGroup([['general', _('General'), general], ['daily', _('Daily'), daily],
		['weekly', _('Weekly'), weekly], ['monthly', _('Monthly'), monthly]]), sentence);
	// The sentence, as each field changes (RefreshDescription).
	const refresh = () => {
		sentence.textContent = describe(edited());
	};
	element.addEventListener('input', refresh);
	element.addEventListener('change', refresh);
	refresh();
	return { element, edited };
}

/**
 * The sentence a schedule reads as (ibJobScheduleRules::Describe): its interval in the coarsest unit the number allows,
 * then only what is not left at «any».
 * @param {StoredSchedule} schedule
 */
export function describe(schedule) {
	let out = intervalText(schedule.IntervalSeconds);
	if (schedule.StartMinute >= 0 && schedule.EndMinute >= 0)
		out += `, ${clockText(schedule.StartMinute)}-${clockText(schedule.EndMinute)}`;
	if (schedule.DaysOfWeek !== 0 && schedule.DaysOfWeek !== anyDay)
		out += `, ${namesOf(schedule.DaysOfWeek, weekDaysShort())}`;
	if (schedule.DaysOfMonth !== 0)
		out += format(_(', on day %s'), namesOf(schedule.DaysOfMonth, monthDays()));
	if (schedule.Months !== 0)
		out += `, ${namesOf(schedule.Months, monthsShort())}`;
	if (schedule.ActiveFrom)
		out += format(_(', from %s'), dayText(schedule.ActiveFrom));
	if (schedule.ActiveTo)
		out += format(_(', until %s'), dayText(schedule.ActiveTo));
	return out;
}

/**
 * Whether a schedule can ever run (ibJobScheduleRules::IsValid) — one that cannot is refused in front of the person
 * who made it, not left to be found as a job that never runs.
 * @param {StoredSchedule} schedule
 */
export function isValid(schedule) {
	const start = schedule.StartMinute;
	const end = schedule.EndMinute;
	const day = 24 * 60;
	if (!(schedule.IntervalSeconds > 0))
		return false;
	// A half-declared window, one out of the day, or an empty one.
	if ((start >= 0 || end >= 0) && (start < 0 || end < 0 || start >= day || end > day || start === end))
		return false;
	if ((schedule.DaysOfMonth !== 0 && (schedule.DaysOfMonth & 0x7FFFFFFF) === 0)
		|| (schedule.DaysOfMonthFromEnd !== 0 && (schedule.DaysOfMonthFromEnd & 0x7FFFFFFF) === 0))
		return false;
	// An ordinal counts one weekday: with several, or none, it has nothing to count.
	if (schedule.WeekdayOrdinal !== 0) {
		const days = schedule.DaysOfWeek === 0 ? anyDay : schedule.DaysOfWeek;
		if ((schedule.WeekdayOrdinal > 5 && schedule.WeekdayOrdinal !== lastOrdinal) || (days & (days - 1)) !== 0)
			return false;
	}
	// A stop before the window opens shuts it for good.
	const stop = schedule.StopAfterMinute;
	if (stop >= 0 && (stop > day || (start >= 0 && stop <= start)))
		return false;
	if (schedule.Months !== 0 && (schedule.Months & 0x0FFF) === 0)
		return false;
	// An inverted validity names no moment (the texts compare as the dates do).
	return !(schedule.ActiveFrom && schedule.ActiveTo && schedule.ActiveTo < schedule.ActiveFrom);
}

/**
 * Seconds in the largest unit that divides them (SplitInterval): «every 2 hours» rather than «every 7200 seconds»; one
 * that divides by none stays in seconds rather than be rounded into a lie.
 * @param {number} seconds
 * @returns {[number, number]} the value and its unit's index
 */
function splitInterval(seconds) {
	for (let at = unitSeconds.length - 1; at >= 0; at--) {
		const unit = unitSeconds[at];
		if (seconds >= unit && seconds % unit === 0)
			return [seconds / unit, at];
	}
	return [seconds > 0 ? seconds : 1, 0];
}

/** «Daily», «Every 6 hours», «Every 10 minutes» (FormatInterval). @param {number} seconds */
function intervalText(seconds) {
	if (seconds % 86400 === 0)
		return seconds === 86400 ? _('Daily') : format(_('Every %d days'), seconds / 86400);
	if (seconds % 3600 === 0)
		return seconds === 3600 ? _('Hourly') : format(_('Every %d hours'), seconds / 3600);
	if (seconds % 60 === 0)
		return seconds === 60 ? _('Every minute') : format(_('Every %d minutes'), seconds / 60);
	return format(_('Every %d seconds'), seconds);
}

/**
 * The names of a mask's bits, in its order.
 * @param {number} mask
 * @param {string[]} names
 */
function namesOf(mask, names) {
	return names.filter((name, at) => (mask & (1 << at)) !== 0).join(', ');
}

/** «02:30» from minutes from midnight (FormatMinute). @param {number} minutes */
function clockText(minutes) {
	const two = (/** @type {number} */ value) => String(value).padStart(2, '0');
	return `${two(Math.floor(minutes / 60))}:${two(minutes % 60)}`;
}

/** A validity bound as a day, dd.MM.yyyy. @param {string} stored */
function dayText(stored) {
	const [year, month, day] = stored.slice(0, 10).split('-');
	return `${day}.${month}.${year}`;
}

/**
 * A time picked, as minutes from midnight — none picked: unset (MinutesFrom).
 * @param {HTMLInputElement} input
 */
function minutesOf(input) {
	const [hours, minutes] = input.value.split(':').map(Number);
	return input.value === '' ? -1 : hours * 60 + minutes;
}

/**
 * A validity bound put in: its day, checked; unbounded — unchecked, the picker on today, as a picker comes.
 * @param {HTMLInputElement} use
 * @param {HTMLInputElement} input
 * @param {string | undefined} stored
 */
function putDay(use, input, stored) {
	use.checked = typeof stored === 'string' && stored !== '';
	const today = new Date();
	const two = (/** @type {number} */ value) => String(value).padStart(2, '0');
	input.value = use.checked ? String(stored).slice(0, 10)
		: `${today.getFullYear()}-${two(today.getMonth() + 1)}-${two(today.getDate())}`;
}

/**
 * Lines that are checked or not, in a box of their own (ibCheckListBox) — a bit of a mask each.
 * @param {string[]} captions
 */
function checkList(captions) {
	const element = document.createElement('div');
	element.className = 'job-schedule-list';
	const boxes = captions.map((text) => {
		const one = checkBox();
		element.append(labelled(one, text));
		return one;
	});
	return {
		element,
		/** @param {number} mask */
		put: (mask) => boxes.forEach((one, at) => { one.checked = (mask & (1 << at)) !== 0; }),
		mask: () => boxes.reduce((mask, one, at) => (one.checked ? mask | (1 << at) : mask), 0),
	};
}

/** @param {...(Node | string)} content */
function page(...content) {
	const element = document.createElement('div');
	element.className = 'job-schedule-page';
	element.append(...content);
	return element;
}

/**
 * A static box: its caption on its frame, its lines inside.
 * @param {string} title
 * @param {...Node} content
 */
function box(title, ...content) {
	const element = document.createElement('fieldset');
	const legend = document.createElement('legend');
	legend.textContent = title;
	element.append(legend, ...content);
	return element;
}

/** @param {...(Node | string)} content */
function line(...content) {
	const element = document.createElement('div');
	element.className = 'job-schedule-line';
	element.append(...content);
	return element;
}

/** @param {string} text */
function caption(text) {
	const element = document.createElement('span');
	element.textContent = text;
	return element;
}

/**
 * @param {HTMLInputElement} input
 * @param {string} text
 */
function labelled(input, text) {
	const label = document.createElement('label');
	label.append(input, text);
	return label;
}

function checkBox() {
	const input = document.createElement('input');
	input.type = 'checkbox';
	return input;
}

/**
 * @param {number} min
 * @param {number} max
 */
function numberInput(min, max) {
	const input = document.createElement('input');
	input.type = 'number';
	input.min = String(min);
	input.max = String(max);
	return input;
}

function dayInput() {
	const input = document.createElement('input');
	input.type = 'date';
	return input;
}

function timeInput() {
	const input = document.createElement('input');
	input.type = 'time';
	return input;
}

/** @param {string[]} captions */
function choice(captions) {
	const select = document.createElement('select');
	for (const text of captions)
		select.append(new Option(text));
	return select;
}
