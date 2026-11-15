// A CHART BOX — its place on the form, empty: the thin client draws nothing in it yet (ibValueChartBox, a panel), nor
// does the page. Its colours and sizes are every control's.

import { Control, defineControl } from './control.js';

class Chartbox extends Control {
}

defineControl('Chartbox', Chartbox);
