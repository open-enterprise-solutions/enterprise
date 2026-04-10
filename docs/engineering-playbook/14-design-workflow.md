# 14. Дизайн UI и пользовательский интерфейс

## Инструменты дизайна для wxWidgets

### Когда использовать графические инструменты

| Сценарий | Инструмент |
|----------|-----------|
| Прототип нового диалога / окна | wxFormBuilder или ручной эскиз |
| Сложная компоновка с вложенными Sizer'ами | wxFormBuilder (генерирует C++ код) |
| Таблицы и гриды данных | wxGrid + ручная настройка |
| Дизайн кастомного виджета | Эскиз на бумаге / Figma для концепции |
| Отчёты и печатные формы | Дизайнер отчётов OES (встроенный) |

**Правило:** для стандартных диалогов — wxFormBuilder. Для сложных пользовательских виджетов — сначала эскиз + обсуждение, затем код.

---

## Дизайн-система OES

### Принципы

1. **Нативность** — UI должен выглядеть нативно на целевой платформе. Не имитировать стили других ОС.
2. **Консистентность** — одинаковые отступы, размеры шрифтов, поведение кнопок во всём приложении.
3. **Доступность** — поддержка клавиатурной навигации, корректные Tab-порядки, метки для всех контролов.
4. **Производительность** — виджеты создаются один раз, обновляется только содержимое (не пересоздание).

### Токены дизайна (C++ константы)

```cpp
// ui_constants.h — единое место для всех визуальных констант

namespace OesUI
{
    // Отступы
    constexpr int MARGIN_SMALL  =  4;
    constexpr int MARGIN_NORMAL =  8;
    constexpr int MARGIN_LARGE  = 16;
    constexpr int PADDING_DIALOG = 12;

    // Размеры кнопок
    // Примечание: constexpr wxSize требует wxWidgets 3.2+.
    // При использовании более ранних версий замените constexpr на const.
    constexpr wxSize BTN_SIZE_NORMAL  { 90, 28 };
    constexpr wxSize BTN_SIZE_WIDE    { 120, 28 };
    constexpr wxSize BTN_SIZE_ICON    { 28, 28 };

    // Минимальные размеры диалогов
    constexpr wxSize DLG_MIN_SMALL    { 320, 200 };
    constexpr wxSize DLG_MIN_NORMAL   { 480, 320 };
    constexpr wxSize DLG_MIN_LARGE    { 640, 480 };

    // Шрифты
    inline wxFont GetMonoFont(int ptSize = 9)
    {
        return wxFont(ptSize, wxFONTFAMILY_TELETYPE,
                      wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    }

    inline wxFont GetBoldFont(int ptSize = 0)
    {
        wxFont f = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        if (ptSize > 0) f.SetPointSize(ptSize);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        return f;
    }

    // Цвета (используем системные цвета где возможно)
    inline wxColour GetErrorColour()   { return wxColour(200, 50, 50);   }
    inline wxColour GetWarningColour() { return wxColour(200, 130, 0);   }
    inline wxColour GetSuccessColour() { return wxColour(40, 140, 60);   }
    inline wxColour GetMutedColour()
    {
        wxColour sys = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
        return sys;
    }
}
```

### Типографика

| Применение | Настройка |
|-----------|-----------|
| Основной текст | `wxSYS_DEFAULT_GUI_FONT` (системный) |
| Заголовки диалогов | системный шрифт + `wxFONTWEIGHT_BOLD` |
| Код, SQL, скрипты | `wxFONTFAMILY_TELETYPE`, pt 9 |
| Метки колонок таблиц | системный шрифт + `wxFONTWEIGHT_BOLD` |
| Подсказки / мелкий текст | системный шрифт - 1pt |

---

## Компоновка (Layout)

### Обязательное использование Sizer'ов

**Никогда** не устанавливать абсолютные позиции виджетов (`wxPoint`, `wxSize` в конструкторе). Всегда использовать Sizer'ы — это обеспечивает корректное масштабирование при разных DPI и размерах шрифта.

```cpp
// Правильно — через Sizer
wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

wxBoxSizer* formSizer = new wxBoxSizer(wxHORIZONTAL);
formSizer->Add(new wxStaticText(this, wxID_ANY, "Наименование:"),
               0, wxALIGN_CENTER_VERTICAL | wxRIGHT, OesUI::MARGIN_NORMAL);
formSizer->Add(m_nameCtrl, 1, wxEXPAND);

mainSizer->Add(formSizer, 0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);
mainSizer->Add(CreateButtonSizer(wxOK | wxCANCEL),
               0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);

SetSizerAndFit(mainSizer);

// Неправильно — абсолютное позиционирование
m_nameCtrl = new wxTextCtrl(this, wxID_ANY, "", wxPoint(100, 20), wxSize(200, 24));
```

### Стандартная структура диалога

```cpp
// Шаблон диалога OES
class OesExampleDialog : public wxDialog
{
public:
    OesExampleDialog(wxWindow* parent, const wxString& title)
        : wxDialog(parent, wxID_ANY, title,
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        BuildUI();
        BindEvents();
        SetMinSize(OesUI::DLG_MIN_NORMAL);
        Centre();
    }

private:
    void BuildUI()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

        // Контентная область
        wxPanel* content = new wxPanel(this);
        wxBoxSizer* cs = new wxBoxSizer(wxVERTICAL);
        // ... добавить контролы в cs
        content->SetSizer(cs);

        // Разделитель + кнопки
        root->Add(content, 1, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT,
                  OesUI::PADDING_DIALOG);
        root->Add(CreateButtonSizer(wxOK | wxCANCEL),
                  0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);

        SetSizerAndFit(root);
    }

    void BindEvents()
    {
        Bind(wxEVT_BUTTON, &OesExampleDialog::OnOK, this, wxID_OK);
    }

    void OnOK(wxCommandEvent&)
    {
        if (!Validate()) return;
        EndModal(wxID_OK);
    }
};
```

---

## Стандартные паттерны UI

> **Примечание по i18n:** строковые литералы в примерах ниже написаны напрямую для наглядности.
> В production-коде все отображаемые пользователю строки должны быть обёрнуты в `_()`:
> `wxMessageBox(_("Поле 'Наименование' обязательно для заполнения."), ...)`.

### Формы с валидацией

```cpp
bool OesDocumentDialog::Validate()
{
    wxString name = m_nameCtrl->GetValue().Trim();
    if (name.IsEmpty())
    {
        wxMessageBox("Поле 'Наименование' обязательно для заполнения.",
                     "Ошибка", wxOK | wxICON_WARNING, this);
        m_nameCtrl->SetFocus();
        return false;
    }

    if (name.Length() > 255)
    {
        wxMessageBox("Наименование не должно превышать 255 символов.",
                     "Ошибка", wxOK | wxICON_WARNING, this);
        m_nameCtrl->SetFocus();
        return false;
    }

    return true;
}
```

### Длительные операции: wxProgressDialog

```cpp
void OesReportView::ExportToFile(const wxString& path)
{
    wxProgressDialog progress(
        "Экспорт",
        "Подготовка данных...",
        100,  // максимум
        this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME | wxPD_CAN_ABORT
    );

    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!progress.Update((int)(i * 100 / m_rows.size()),
                wxString::Format("Строка %d из %d...", (int)(i+1), (int)m_rows.size())))
        {
            // Пользователь нажал Отмена
            break;
        }
        // ... запись строки
    }
}
```

### Таблицы (wxGrid)

```cpp
// Настройка внешнего вида грида
void OesDataGrid::ApplyStyle()
{
    SetDefaultCellFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    SetLabelFont(OesUI::GetBoldFont());

    SetRowLabelSize(wxGRID_AUTOSIZE);
    SetColLabelAlignment(wxALIGN_LEFT, wxALIGN_CENTER);

    EnableGridLines(true);
    SetGridLineColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW));

    // Чередование цветов строк
    SetDefaultCellBackgroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    // Чётные строки — лёгкая подсветка через wxGridCellAttr
}
```

### Контекстное меню

```cpp
void OesListPanel::OnContextMenu(wxContextMenuEvent& event)
{
    wxMenu menu;
    menu.Append(ID_OPEN,   "Открыть\tEnter");
    menu.Append(ID_EDIT,   "Редактировать\tF2");
    menu.AppendSeparator();
    menu.Append(ID_DELETE, "Удалить\tDel");
    menu.Append(ID_EXPORT, "Экспортировать...");

    // Блокировать пункты если нет выбора
    if (m_list->GetSelectedItemCount() == 0)
    {
        menu.Enable(ID_OPEN,   false);
        menu.Enable(ID_EDIT,   false);
        menu.Enable(ID_DELETE, false);
        menu.Enable(ID_EXPORT, false);
    }

    PopupMenu(&menu);
}
```

---

## Доступность (Accessibility)

### Обязательные требования

- **Tab-порядок**: все интерактивные контролы доступны с клавиатуры. Порядок обхода — слева направо, сверху вниз.
- **Метки**: каждый `wxTextCtrl`, `wxComboBox`, `wxSpinCtrl` должен иметь ассоциированный `wxStaticText` (создаётся через `wxStaticText` + Sizer или через `wxWindow::SetLabel`).
- **Горячие клавиши**: для часто используемых действий назначить акселераторы (`wxAcceleratorTable` или `&` в тексте меню).
- **Подсказки**: `SetToolTip()` для кнопок-иконок и неочевидных элементов.
- **Сообщения об ошибках**: использовать `wxMessageBox` с `wxICON_WARNING` или `wxICON_ERROR`; не скрывать ошибки молча.

```cpp
// Правильно — метка ассоциирована с полем ввода
wxStaticText* lblName = new wxStaticText(this, wxID_ANY, "&Наименование:");
m_nameCtrl = new wxTextCtrl(this, wxID_ANY);
m_nameCtrl->SetToolTip("Введите наименование документа (до 255 символов)");

// Подсказки для кнопок-иконок
m_btnAdd->SetToolTip("Добавить запись (Ins)");
m_btnDel->SetToolTip("Удалить выбранное (Del)");
```

### Tab-порядок

wxWidgets автоматически использует порядок создания виджетов как Tab-порядок. Создавать контролы в логическом порядке: поле 1, поле 2, ..., OK, Cancel.

---

## AI и генерация UI-кода

### Когда AI помогает в разработке wxWidgets UI

| Задача | Использование AI |
|--------|-----------------|
| Генерация шаблона диалога | Описать структуру — AI генерирует каркас |
| Настройка wxGrid | Попросить готовый пример с нужными колонками |
| Реализация сортировки/фильтрации в списке | Алгоритмы + wxListCtrl или wxGrid |
| Написание обработчиков событий | AI генерирует по описанию действия |

### Правила переноса AI-кода

**Обязательно проверить:**
- Все константы отступов заменены на `OesUI::MARGIN_*`
- Нет абсолютного позиционирования — только Sizer'ы
- Шрифты не захардкожены — используются `wxSystemSettings` или `OesUI::Get*Font()`
- Цвета используют системные `wxSYS_COLOUR_*` или константы из `OesUI`
- Event IDs объявлены в enum модуля, не как magic numbers
- Нет утечек памяти: wx-объекты переданы родителю или добавлены в Sizer (который управляет памятью)

**Чеклист после переноса AI-кода:**
- [ ] Диалог корректно масштабируется при изменении размера
- [ ] Tab-порядок логичен
- [ ] Все кнопки-иконки имеют подсказки
- [ ] Валидация данных перед `EndModal(wxID_OK)`
- [ ] Сборка без предупреждений компилятора
- [ ] Проверка на разных системных шрифтах и DPI

---

## wxFormBuilder

### Рабочий процесс

1. Создать макет в wxFormBuilder
2. Задать все имена переменных в соответствии с конвенцией проекта (`m_nameCtrl`, `m_listGrid`)
3. Экспортировать как `.cpp`/`.h` пару (`form_generated.cpp`, `form_generated.h`)
4. Создать наследника, никогда не редактировать сгенерированный файл вручную
5. Всю бизнес-логику реализовывать в наследнике

```cpp
// form_generated.h — НЕ редактировать
class OesDocumentFormBase : public wxDialog { ... };

// document_dialog.h — только бизнес-логика
class OesDocumentDialog : public OesDocumentFormBase
{
public:
    OesDocumentDialog(wxWindow* parent);
    DocumentData GetResult() const;

protected:
    void OnOKClicked(wxCommandEvent& event) override;
    void OnNameChanged(wxCommandEvent& event) override;

private:
    bool ValidateInput();
};
```

### Соглашения именования в wxFormBuilder

| Элемент | Шаблон | Пример |
|---------|--------|--------|
| Класс формы | `Oes<Name>FormBase` | `OesDocumentFormBase` |
| TextCtrl | `m_<name>Ctrl` | `m_titleCtrl` |
| ComboBox | `m_<name>Combo` | `m_statusCombo` |
| Grid | `m_<name>Grid` | `m_itemsGrid` |
| Button | `m_btn<Action>` | `m_btnAdd`, `m_btnOK` |
| CheckBox | `m_chk<Name>` | `m_chkActive` |
| StaticText (метка) | `m_lbl<Name>` | `m_lblTitle` |

---

## Чеклист UI перед сдачей

- [ ] Диалог открывается и закрывается без утечек памяти (запустить под Dr. Memory)
- [ ] Корректное поведение при минимальном размере окна
- [ ] Tab-порядок проверен (прохождение по Tab через все контролы)
- [ ] Горячие клавиши работают (Enter = OK, Escape = Cancel)
- [ ] Сообщения об ошибках понятны пользователю
- [ ] Длинные операции показывают прогресс (wxProgressDialog)
- [ ] Нет захардкоженных русских строк в C++ коде — только через `_()` или ресурсы (готовность к i18n)
- [ ] Внешний вид проверен на Windows 10/11 с разными темами (светлая/тёмная)
