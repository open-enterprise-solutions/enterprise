/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  registerTranslation
} from "../chunks/chunk.HOKYDFUG.js";
import "../chunks/chunk.JHZRD2LV.js";

// src/translations/hr.ts
var translation = {
  $code: "hr",
  $name: "Hrvatski",
  $dir: "ltr",
  allTagsRemoved: "Sve oznake uklonjene",
  am: "AM",
  autosizeColumn: "Automatski prilagodi veli\u010Dinu stupca",
  captions: "Titlovi",
  carousel: "Vrtuljak",
  chooseDate: "Odaberi datum",
  chooseDecade: "Odaberi desetlje\u0107e",
  chooseMonth: "Odaberi mjesec",
  chooseTime: "Odaberi vrijeme",
  chooseYear: "Odaberi godinu",
  clearEntry: "O\u010Disti unos",
  clearFilter: "Poni\u0161ti filtar",
  clearSort: "Poni\u0161ti sortiranje",
  close: "Zatvori",
  closeCalendar: "Zatvori kalendar",
  closeTimeInput: "Zatvori bira\u010D vremena",
  collapseRow: "Sa\u017Emi redak",
  columnMenu: "Opcije stupca",
  columnMovedToPosition: (label, position, total) => `${label} premje\u0161ten na poziciju ${position} od ${total}`,
  columns: "Stupci",
  compactPageXOfY: (page, total) => `${page} od ${total}`,
  completed: "Dovr\u0161eno",
  copied: "Kopirano",
  copy: "Kopiraj",
  createOption: (value) => `Stvori "${value}"`,
  currentlyPlaying: "trenutno se reproducira",
  currentValue: "Trenutna vrijednost",
  date: "Datum",
  datePickerKeyboardHelp: "Strelicama mijenjajte vrijednosti; pritisnite Alt+Strelica dolje za otvaranje kalendara.",
  day: "Dan",
  dayPeriod: "AM/PM",
  decrement: "Smanji",
  deselectAllRows: "Poni\u0161ti odabir svih redaka",
  disabled: "Onemogu\u0107eno",
  dropFileHere: "Drop file here or click to browse",
  dropFilesHere: "Drop files here or click to browse",
  empty: "Prazno",
  endDate: "Datum zavr\u0161etka",
  enterFullscreen: "U\u0111i u cijeli zaslon",
  error: "Gre\u0161ka",
  exitFullscreen: "Iza\u0111i iz cijelog zaslona",
  expandRow: "Pro\u0161iri redak",
  filterByColumn: (label) => `Filtriraj po stupcu ${label}`,
  filterFrom: "Od",
  filterMax: "Maks",
  filterMin: "Min",
  filterTo: "Do",
  firstPage: "Prva stranica",
  goToSlide: (slide, count) => `Idi na slajd ${slide} od ${count}`,
  hideColumn: "Sakrij stupac",
  hidePassword: "Sakrij lozinku",
  hour: "Sat",
  incompleteDate: "Unesite valjani datum.",
  increment: "Pove\u0107aj",
  jumpBackwardX: (count) => {
    const mod10 = count % 10;
    const mod100 = count % 100;
    if (mod10 === 1 && mod100 !== 11) return `Sko\u010Di ${count} stranicu unatrag`;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Sko\u010Di ${count} stranice unatrag`;
    return `Sko\u010Di ${count} stranica unatrag`;
  },
  jumpForwardX: (count) => {
    const mod10 = count % 10;
    const mod100 = count % 100;
    if (mod10 === 1 && mod100 !== 11) return `Sko\u010Di ${count} stranicu unaprijed`;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Sko\u010Di ${count} stranice unaprijed`;
    return `Sko\u010Di ${count} stranica unaprijed`;
  },
  lastPage: "Posljednja stranica",
  loading: "U\u010Ditavanje",
  locked: "Zaklju\u010Dano",
  minute: "Minuta",
  month: "Mjesec",
  moreOptions: "Vi\u0161e opcija",
  mute: "Uti\u0161aj",
  nextDecade: "Sljede\u0107e desetlje\u0107e",
  nextMonth: "Sljede\u0107i mjesec",
  nextPage: "Sljede\u0107a stranica",
  nextSlide: "Sljede\u0107i slajd",
  nextVideo: "Sljede\u0107i video",
  nextYear: "Sljede\u0107a godina",
  noData: "Nema podataka",
  noOptions: "Nema opcija",
  noResults: "Nema odgovaraju\u0107ih rezultata",
  notCompleted: "Nije dovr\u0161eno",
  now: "Sada",
  numCharacters: (num) => {
    if (num === 1) return "1 znak";
    const mod10 = num % 10;
    const mod100 = num % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `${num} znaka`;
    return `${num} znakova`;
  },
  numCharactersRemaining: (num) => {
    if (num === 1) return "1 preostali znak";
    const mod10 = num % 10;
    const mod100 = num % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `${num} preostala znaka`;
    return `${num} preostalih znakova`;
  },
  numOptionsAvailable: (num) => {
    if (num === 0) return "Nema dostupnih opcija";
    if (num === 1) return "1 opcija je dostupna";
    const mod10 = num % 10;
    const mod100 = num % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `${num} opcije su dostupne`;
    return `${num} opcija je dostupno`;
  },
  numOptionsSelected: (num) => {
    if (num === 0) return "Nije odabrana nijedna opcija";
    if (num === 1) return "1 opcija je odabrana";
    return `${num} odabranih opcija`;
  },
  numRowsCopied: (num) => {
    if (num === 1) return "1 redak kopiran";
    const mod10 = num % 10;
    const mod100 = num % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `${num} retka kopirana`;
    return `${num} redaka kopirano`;
  },
  numRowsSelected: (num) => {
    if (num === 1) return "1 redak odabran";
    const mod10 = num % 10;
    const mod100 = num % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `${num} retka odabrana`;
    return `${num} redaka odabrano`;
  },
  optionPosition: (label, position, total) => `${label}, ${position} od ${total}`,
  optionsLoadError: "Opcije se ne mogu u\u010Ditati",
  pageXOfY: (page, total) => `Stranica ${page} od ${total}`,
  pagination: "Strani\u010Denje",
  pause: "Pauziraj",
  pauseAnimation: "Pauziraj animaciju",
  pictureInPicture: "Slika u slici",
  pinLeft: "Prikva\u010Di lijevo",
  pinRight: "Prikva\u010Di desno",
  play: "Reproduciraj",
  playAnimation: "Reproduciraj animaciju",
  playbackSpeed: "Brzina reprodukcije",
  playlist: "Popis za reprodukciju",
  pm: "PM",
  previousDecade: "Prethodno desetlje\u0107e",
  previousMonth: "Prethodni mjesec",
  previousPage: "Prethodna stranica",
  previousSlide: "Prethodni slajd",
  previousVideo: "Prethodni video",
  previousYear: "Prethodna godina",
  progress: "Napredak",
  rangeTooLong: (max) => {
    if (max === 1) return "Odaberite raspon ne dulji od 1 dana";
    const mod10 = max % 10;
    const mod100 = max % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Odaberite raspon ne dulji od ${max} dana`;
    return `Odaberite raspon ne dulji od ${max} dana`;
  },
  rangeTooShort: (min) => {
    if (min === 1) return "Odaberite raspon dug najmanje 1 dan";
    const mod10 = min % 10;
    const mod100 = min % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Odaberite raspon dug najmanje ${min} dana`;
    return `Odaberite raspon dug najmanje ${min} dana`;
  },
  readonly: "Samo za \u010Ditanje",
  remove: "Makni",
  resetColumns: "Vrati zadane stupce",
  resize: "Promijeni veli\u010Dinu",
  resizeColumn: "Promijeni veli\u010Dinu stupca",
  rowsPerPage: "Redaka po stranici",
  scrollableRegion: "Podru\u010Dje s mogu\u0107no\u0161\u0107u pomicanja",
  scrollToEnd: "Skrolaj do kraja",
  scrollToStart: "Skrolaj na po\u010Detak",
  search: "Pretra\u017Ei",
  second: "Sekunda",
  seek: "Tra\u017Ei",
  seekProgress: (current, duration) => `${current} od ${duration}`,
  selectAColorFromTheScreen: "Odaberi boju sa ekrana",
  selectAllRows: "Odaberi sve retke",
  selected: "Odabrano",
  selectedDateLabel: (date) => `Odabrano: ${date}`,
  selectedRangeLabel: (range) => `Odabrani raspon: ${range}`,
  selectGroup: "Odaberi grupu",
  selectionCleared: "Odabir poni\u0161ten",
  selectRow: "Odaberi redak",
  showingNofMRows: (shown, total) => `Prikazuje se ${shown} od ${total} redaka`,
  showingXtoYofZ: (start, end, total) => `${start}\u2013${end} od ${total}`,
  showPassword: "Poka\u017Ei lozinku",
  slideNum: (slide) => `Slajd ${slide}`,
  sortAscending: "Sortiraj uzlazno",
  sortColumn: "Sortiraj stupac",
  sortDescending: "Sortiraj silazno",
  startDate: "Datum po\u010Detka",
  steps: "Koraci",
  stepXOfY: (step, total) => `Korak ${step} od ${total}`,
  tagAdded: (tag) => `${tag} dodano`,
  tagAlreadyAdded: (tag) => `Oznaka ${tag} ve\u0107 postoji`,
  tagInputKeyboardHelp: "Pritisnite Backspace ili Delete za uklanjanje ove oznake.",
  tagRemoved: (tag) => `${tag} uklonjeno`,
  time: "Vrijeme",
  timeInputKeyboardHelp: "Strelicama mijenjajte vrijednosti; pritisnite Alt+Strelica dolje za otvaranje bira\u010Da vremena.",
  today: "Danas",
  toggleColorFormat: "Zamijeni format boje",
  tooFewTags: (min) => {
    if (min === 1) return "Dodajte najmanje 1 oznaku";
    const mod10 = min % 10;
    const mod100 = min % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Dodajte najmanje ${min} oznake`;
    return `Dodajte najmanje ${min} oznaka`;
  },
  tooManyTags: (max) => {
    if (max === 1) return "Dodajte najvi\u0161e 1 oznaku";
    const mod10 = max % 10;
    const mod100 = max % 100;
    if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return `Dodajte najvi\u0161e ${max} oznake`;
    return `Dodajte najvi\u0161e ${max} oznaka`;
  },
  unmute: "Uklju\u010Di zvuk",
  unpin: "Otkva\u010Di",
  unpinColumn: "Otkva\u010Di stupac",
  videoPlayer: "Video player",
  volume: "Glasno\u0107a",
  year: "Godina",
  zoomIn: "Pove\u0107aj",
  zoomOut: "Smanji"
};
registerTranslation(translation);
var hr_default = translation;
export {
  hr_default as default
};
