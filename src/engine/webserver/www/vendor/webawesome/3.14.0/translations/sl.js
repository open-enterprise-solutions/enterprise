/*! Copyright 2026 Fonticons, Inc. - https://webawesome.com/license */
import {
  registerTranslation
} from "../chunks/chunk.HOKYDFUG.js";
import "../chunks/chunk.JHZRD2LV.js";

// src/translations/sl.ts
var translation = {
  $code: "sl",
  $name: "Slovenski",
  $dir: "ltr",
  allTagsRemoved: "Vse oznake odstranjene",
  am: "AM",
  autosizeColumn: "Samodejno prilagodi velikost stolpca",
  captions: "Podnapisi",
  carousel: "Vrtiljak",
  chooseDate: "Izberite datum",
  chooseDecade: "Izberite desetletje",
  chooseMonth: "Izberite mesec",
  chooseTime: "Izberite \u010Das",
  chooseYear: "Izberite leto",
  clearEntry: "Po\u010Disti vnos",
  clearFilter: "Po\u010Disti filter",
  clearSort: "Po\u010Disti razvr\u0161\u010Danje",
  close: "Zapri",
  closeCalendar: "Zapri koledar",
  closeTimeInput: "Zapri izbirnik \u010Dasa",
  collapseRow: "Strni vrstico",
  columnMenu: "Mo\u017Enosti stolpca",
  columnMovedToPosition: (label, position, total) => `${label} premaknjen na polo\u017Eaj ${position} od ${total}`,
  columns: "Stolpci",
  compactPageXOfY: (page, total) => `${page} od ${total}`,
  completed: "Dokon\u010Dano",
  copied: "Kopirano",
  copy: "Kopiraj",
  createOption: (value) => `Ustvari "${value}"`,
  currentlyPlaying: "se trenutno predvaja",
  currentValue: "Trenutna vrednost",
  date: "Datum",
  datePickerKeyboardHelp: "S pu\u0161\u010Di\u010Dnimi tipkami spreminjajte vrednosti; pritisnite Alt+Pu\u0161\u010Dica navzdol za odpiranje koledarja.",
  day: "Dan",
  dayPeriod: "AM/PM",
  decrement: "Zmanj\u0161aj",
  deselectAllRows: "Prekli\u010Di izbiro vseh vrstic",
  disabled: "Onemogo\u010Deno",
  dropFileHere: "Drop file here or click to browse",
  dropFilesHere: "Drop files here or click to browse",
  empty: "Prazno",
  endDate: "Kon\u010Dni datum",
  enterFullscreen: "Vstopi v celozaslonski na\u010Din",
  error: "Napaka",
  exitFullscreen: "Zapusti celozaslonski na\u010Din",
  expandRow: "Raz\u0161iri vrstico",
  filterByColumn: (label) => `Filtriraj po ${label}`,
  filterFrom: "Od",
  filterMax: "Najv.",
  filterMin: "Najm.",
  filterTo: "Do",
  firstPage: "Prva stran",
  goToSlide: (slide, count) => `Pojdi na diapozitiv ${slide} od ${count}`,
  hideColumn: "Skrij stolpec",
  hidePassword: "Skrij geslo",
  hour: "Ura",
  incompleteDate: "Vnesite veljaven datum.",
  increment: "Pove\u010Daj",
  jumpBackwardX: (count) => {
    const mod100 = count % 100;
    if (mod100 === 1) return `Pomakni se ${count} stran nazaj`;
    if (mod100 === 2) return `Pomakni se ${count} strani nazaj`;
    if (mod100 === 3 || mod100 === 4) return `Pomakni se ${count} strani nazaj`;
    return `Pomakni se ${count} strani nazaj`;
  },
  jumpForwardX: (count) => {
    const mod100 = count % 100;
    if (mod100 === 1) return `Pomakni se ${count} stran naprej`;
    if (mod100 === 2) return `Pomakni se ${count} strani naprej`;
    if (mod100 === 3 || mod100 === 4) return `Pomakni se ${count} strani naprej`;
    return `Pomakni se ${count} strani naprej`;
  },
  lastPage: "Zadnja stran",
  loading: "Nalaganje",
  locked: "Zaklenjeno",
  minute: "Minuta",
  month: "Mesec",
  moreOptions: "Ve\u010D mo\u017Enosti",
  mute: "Uti\u0161aj",
  nextDecade: "Naslednje desetletje",
  nextMonth: "Naslednji mesec",
  nextPage: "Naslednja stran",
  nextSlide: "Naslednji diapozitiv",
  nextVideo: "Naslednji videoposnetek",
  nextYear: "Naslednje leto",
  noData: "Ni podatkov",
  noOptions: "Ni mo\u017Enosti",
  noResults: "Ni ustreznih rezultatov",
  notCompleted: "Ni dokon\u010Dano",
  now: "Zdaj",
  numCharacters: (num) => {
    const mod100 = num % 100;
    if (mod100 === 1) return `${num} znak`;
    if (mod100 === 2) return `${num} znaka`;
    if (mod100 === 3 || mod100 === 4) return `${num} znaki`;
    return `${num} znakov`;
  },
  numCharactersRemaining: (num) => {
    const mod100 = num % 100;
    if (mod100 === 1) return `Preostane ${num} znak`;
    if (mod100 === 2) return `Preostaneta ${num} znaka`;
    if (mod100 === 3 || mod100 === 4) return `Preostanejo ${num} znaki`;
    return `Preostane ${num} znakov`;
  },
  numOptionsAvailable: (num) => {
    if (num === 0) return "Ni razpolo\u017Eljivih mo\u017Enosti";
    const mod100 = num % 100;
    if (mod100 === 1) return `Na voljo je ${num} mo\u017Enost`;
    if (mod100 === 2) return `Na voljo sta ${num} mo\u017Enosti`;
    if (mod100 === 3 || mod100 === 4) return `Na voljo so ${num} mo\u017Enosti`;
    return `Na voljo je ${num} mo\u017Enosti`;
  },
  numOptionsSelected: (num) => {
    if (num === 0) return "Nobena mo\u017Enost ni izbrana";
    if (num === 1) return "1 mo\u017Enost izbrana";
    if (num === 2) return "2 mo\u017Enosti izbrani";
    if (num === 3 || num === 4) return `${num} mo\u017Enosti izbrane`;
    return `${num} mo\u017Enosti izbranih`;
  },
  numRowsCopied: (num) => {
    const mod100 = num % 100;
    if (mod100 === 1) return `${num} kopirana vrstica`;
    if (mod100 === 2) return `${num} kopirani vrstici`;
    if (mod100 === 3 || mod100 === 4) return `${num} kopirane vrstice`;
    return `${num} kopiranih vrstic`;
  },
  numRowsSelected: (num) => {
    const mod100 = num % 100;
    if (mod100 === 1) return `${num} izbrana vrstica`;
    if (mod100 === 2) return `${num} izbrani vrstici`;
    if (mod100 === 3 || mod100 === 4) return `${num} izbrane vrstice`;
    return `${num} izbranih vrstic`;
  },
  optionPosition: (label, position, total) => `${label}, ${position} od ${total}`,
  optionsLoadError: "Mo\u017Enosti ni bilo mogo\u010De nalo\u017Eiti",
  pageXOfY: (page, total) => `Stran ${page} od ${total}`,
  pagination: "O\u0161tevil\u010Devanje strani",
  pause: "Premor",
  pauseAnimation: "Zaustavi animacijo",
  pictureInPicture: "Slika v sliki",
  pinLeft: "Pripni levo",
  pinRight: "Pripni desno",
  play: "Predvajaj",
  playAnimation: "Predvajaj animacijo",
  playbackSpeed: "Hitrost predvajanja",
  playlist: "Seznam predvajanja",
  pm: "PM",
  previousDecade: "Prej\u0161nje desetletje",
  previousMonth: "Prej\u0161nji mesec",
  previousPage: "Prej\u0161nja stran",
  previousSlide: "Prej\u0161nji diapozitiv",
  previousVideo: "Prej\u0161nji videoposnetek",
  previousYear: "Prej\u0161nje leto",
  progress: "Napredek",
  rangeTooLong: (max) => {
    const mod100 = max % 100;
    if (mod100 === 1) return `Izberite obdobje, ki ni dalj\u0161e od ${max} dneva`;
    if (mod100 === 2) return `Izberite obdobje, ki ni dalj\u0161e od ${max} dni`;
    if (mod100 === 3 || mod100 === 4) return `Izberite obdobje, ki ni dalj\u0161e od ${max} dni`;
    return `Izberite obdobje, ki ni dalj\u0161e od ${max} dni`;
  },
  rangeTooShort: (min) => {
    const mod100 = min % 100;
    if (mod100 === 1) return `Izberite obdobje, dolgo vsaj ${min} dan`;
    if (mod100 === 2) return `Izberite obdobje, dolgo vsaj ${min} dneva`;
    if (mod100 === 3 || mod100 === 4) return `Izberite obdobje, dolgo vsaj ${min} dni`;
    return `Izberite obdobje, dolgo vsaj ${min} dni`;
  },
  readonly: "Samo za branje",
  remove: "Odstrani",
  resetColumns: "Ponastavi stolpce",
  resize: "Spremeni velikost",
  resizeColumn: "Spremeni velikost stolpca",
  rowsPerPage: "Vrstic na stran",
  scrollableRegion: "Podro\u010Dje za drsenje",
  scrollToEnd: "Pomakni se na konec",
  scrollToStart: "Pomakni se na za\u010Detek",
  search: "Iskanje",
  second: "Sekunda",
  seek: "I\u0161\u010Di",
  seekProgress: (current, duration) => `${current} od ${duration}`,
  selectAColorFromTheScreen: "Izberite barvo z zaslona",
  selectAllRows: "Izberi vse vrstice",
  selected: "Izbrano",
  selectedDateLabel: (date) => `Izbrano: ${date}`,
  selectedRangeLabel: (range) => `Izbrano obdobje: ${range}`,
  selectGroup: "Izberi skupino",
  selectionCleared: "Izbira po\u010Di\u0161\u010Dena",
  selectRow: "Izberi vrstico",
  showingNofMRows: (shown, total) => `Prikazanih ${shown} od ${total} vrstic`,
  showingXtoYofZ: (start, end, total) => `${start}\u2013${end} od ${total}`,
  showPassword: "Prika\u017Ei geslo",
  slideNum: (slide) => `Diapozitiv ${slide}`,
  sortAscending: "Razvrsti nara\u0161\u010Dajo\u010De",
  sortColumn: "Razvrsti stolpec",
  sortDescending: "Razvrsti padajo\u010De",
  startDate: "Za\u010Detni datum",
  steps: "Koraki",
  stepXOfY: (step, total) => `Korak ${step} od ${total}`,
  tagAdded: (tag) => `${tag} dodano`,
  tagAlreadyAdded: (tag) => `Oznaka ${tag} \u017Ee obstaja`,
  tagInputKeyboardHelp: "Pritisnite Backspace ali Delete, da odstranite to oznako.",
  tagRemoved: (tag) => `${tag} odstranjeno`,
  time: "\u010Cas",
  timeInputKeyboardHelp: "S pu\u0161\u010Di\u010Dnimi tipkami spreminjajte vrednosti; pritisnite Alt+Pu\u0161\u010Dica navzdol za odpiranje izbirnika \u010Dasa.",
  today: "Danes",
  toggleColorFormat: "Preklopi format barve",
  tooFewTags: (min) => {
    const mod100 = min % 100;
    if (mod100 === 1) return `Dodajte vsaj ${min} oznako`;
    if (mod100 === 2) return `Dodajte vsaj ${min} oznaki`;
    if (mod100 === 3 || mod100 === 4) return `Dodajte vsaj ${min} oznake`;
    return `Dodajte vsaj ${min} oznak`;
  },
  tooManyTags: (max) => {
    const mod100 = max % 100;
    if (mod100 === 1) return `Dodajte najve\u010D ${max} oznako`;
    if (mod100 === 2) return `Dodajte najve\u010D ${max} oznaki`;
    if (mod100 === 3 || mod100 === 4) return `Dodajte najve\u010D ${max} oznake`;
    return `Dodajte najve\u010D ${max} oznak`;
  },
  unmute: "Vklopi zvok",
  unpin: "Odpni",
  unpinColumn: "Odpni stolpec",
  videoPlayer: "Videopredvajalnik",
  volume: "Glasnost",
  year: "Leto",
  zoomIn: "Pove\u010Daj",
  zoomOut: "Pomanj\u0161aj"
};
registerTranslation(translation);
var sl_default = translation;
export {
  sl_default as default
};
