import { create } from "zustand"
import { persist } from "zustand/middleware"
import { api } from "@/lib/axios"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type AiActionType =
  | "create_object"
  | "write_module"
  | "add_attribute"
  | "generate_form"

export interface AiAction {
  type: AiActionType
  payload: Record<string, unknown>
  label: string
}

export interface AiMessage {
  id: string
  role: "user" | "assistant"
  content: string
  timestamp: number
  actions?: AiAction[]
  isLoading?: boolean
}

export interface AiContext {
  objectType?: string
  objectName?: string
  currentModule?: string
  metadataTree?: unknown
}

interface AiAssistantState {
  messages: AiMessage[]
  context: AiContext
  isOpen: boolean
  isLoading: boolean
  error: string | null

  open: () => void
  close: () => void
  toggle: () => void
  setContext: (ctx: AiContext) => void
  sendMessage: (text: string) => Promise<void>
  applyAction: (action: AiAction) => Promise<void>
  clearHistory: () => void
  dismissError: () => void
}

// ---------------------------------------------------------------------------
// Mock response generator (stubbed until real Claude MCP integration)
// ---------------------------------------------------------------------------

function generateMockResponse(text: string, ctx: AiContext): { content: string; actions: AiAction[] } {
  const lower = text.toLowerCase()

  if (lower.includes("справочник") || lower.includes("catalog")) {
    const nameMatch = text.match(/справочник\s+(\S+)/i) || text.match(/catalog\s+(\S+)/i)
    const name = nameMatch?.[1] ?? "NewCatalog"
    return {
      content: `Создаю справочник **${name}**.

\`\`\`oes
// Модуль объекта — ${name}
Процедура ПередЗаписью(Отказ)
    Если ПустаяСтрока(Наименование) Тогда
        Отказ = Истина;
        Сообщить("Наименование обязательно для заполнения");
    КонецЕсли;
КонецПроцедуры
\`\`\`

Конфигурация справочника будет создана с реквизитами: Код, Наименование. Вы можете добавить дополнительные реквизиты через Designer.`,
      actions: [
        {
          type: "create_object",
          payload: {
            type: "catalog",
            name,
            attributes: [
              { name: "Description", type: "String", required: true },
              { name: "Code", type: "String", required: false },
            ],
          },
          label: `Создать справочник ${name}`,
        },
      ],
    }
  }

  if (lower.includes("документ") || lower.includes("document")) {
    const nameMatch = text.match(/документ\s+(\S+)/i) || text.match(/document\s+(\S+)/i)
    const name = nameMatch?.[1] ?? "NewDocument"
    return {
      content: `Создаю документ **${name}** с табличной частью.

\`\`\`oes
// Модуль проведения — ${name}
Процедура ОбработкаПроведения(Отказ, РежимПроведения)
    // Движения по регистрам
    Движения.Очистить();

    Для Каждого Строка Из Состав Цикл
        Движение = Движения.ОстаткиТоваров.Добавить();
        Движение.Вид = ВидДвиженияНакопления.Приход;
        Движение.Товар = Строка.Товар;
        Движение.Количество = Строка.Количество;
    КонецЦикла;
КонецПроцедуры
\`\`\``,
      actions: [
        {
          type: "create_object",
          payload: {
            type: "document",
            name,
            tabularSections: [
              {
                name: "Состав",
                attributes: [
                  { name: "Товар", type: "CatalogRef.Products" },
                  { name: "Количество", type: "Number" },
                  { name: "Цена", type: "Number" },
                  { name: "Сумма", type: "Number" },
                ],
              },
            ],
          },
          label: `Создать документ ${name}`,
        },
        {
          type: "write_module",
          payload: { objectType: "document", objectName: name, moduleType: "object" },
          label: "Записать модуль проведения",
        },
      ],
    }
  }

  if (lower.includes("модуль") || lower.includes("module") || lower.includes("функци") || lower.includes("процедур")) {
    const objName = ctx.objectName ?? "Объект"
    return {
      content: `Генерирую модуль для **${objName}**.

\`\`\`oes
// Автоматически сгенерированный модуль
// Объект: ${objName}

Процедура ПередЗаписью(Отказ)
    ПроверитьЗаполнение();
КонецПроцедуры

Процедура ПроверитьЗаполнение()
    Если ПустаяСтрока(Наименование) Тогда
        Сообщить("Поле Наименование обязательно");
    КонецЕсли;
КонецПроцедуры

Функция РассчитатьСумму() Экспорт
    Итог = 0;
    Для Каждого Строка Из Состав Цикл
        Строка.Сумма = Строка.Количество * Строка.Цена;
        Итог = Итог + Строка.Сумма;
    КонецЦикла;
    Возврат Итог;
КонецФункции
\`\`\``,
      actions: [
        {
          type: "write_module",
          payload: {
            objectType: ctx.objectType ?? "catalog",
            objectName: objName,
            moduleType: "object",
          },
          label: "Применить модуль",
        },
      ],
    }
  }

  if (lower.includes("реквизит") || lower.includes("attribute") || lower.includes("поле")) {
    return {
      content: `Добавляю реквизиты к объекту **${ctx.objectName ?? "текущему"}**.

Рекомендуемые реквизиты на основе контекста:
- **Наименование** (Строка, 150) — обязательное
- **Комментарий** (Строка, 500) — необязательное
- **Дата** (Дата) — дата создания записи
- **Ответственный** (СправочникСсылка.Пользователи) — ответственный сотрудник`,
      actions: [
        {
          type: "add_attribute",
          payload: {
            objectType: ctx.objectType,
            objectName: ctx.objectName,
            attributes: [
              { name: "Description", type: "String", length: 150, required: true },
              { name: "Comment", type: "String", length: 500 },
              { name: "Date", type: "Date" },
            ],
          },
          label: "Добавить реквизиты",
        },
      ],
    }
  }

  if (lower.includes("форм") || lower.includes("form")) {
    return {
      content: `Генерирую схему формы для **${ctx.objectName ?? "объекта"}**.

Форма будет содержать:
- Шапку с основными реквизитами
- Табличную часть (если есть)
- Кнопки: Записать, Провести, Закрыть`,
      actions: [
        {
          type: "generate_form",
          payload: {
            objectType: ctx.objectType,
            objectName: ctx.objectName,
            formType: "object",
          },
          label: "Сгенерировать форму",
        },
      ],
    }
  }

  return {
    content: `Понял ваш запрос. Для более точной генерации конфигурации уточните:

1. Какой тип объекта нужно создать? (Справочник, Документ, Регистр)
2. Какие реквизиты должны быть?
3. Нужны ли табличные части?

**Примеры запросов:**
- "Создай справочник Контрагенты с реквизитами ИНН, КПП, Адрес"
- "Напиши модуль проведения для документа Заказ"
- "Добавь табличную часть Товары к документу Накладная"
- "Создай отчёт по остаткам товаров"`,
    actions: [],
  }
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

let messageCounter = 0
function nextId() {
  return `msg_${Date.now()}_${++messageCounter}`
}

export const useAiAssistant = create<AiAssistantState>()(
  persist(
    (set, get) => ({
      messages: [],
      context: {},
      isOpen: false,
      isLoading: false,
      error: null,

      open: () => set({ isOpen: true }),
      close: () => set({ isOpen: false }),
      toggle: () => set((s) => ({ isOpen: !s.isOpen })),

      setContext: (ctx: AiContext) => set({ context: ctx }),

      dismissError: () => set({ error: null }),

      clearHistory: () => set({ messages: [] }),

      sendMessage: async (text: string) => {
        const { messages, context } = get()

        const userMsg: AiMessage = {
          id: nextId(),
          role: "user",
          content: text,
          timestamp: Date.now(),
        }

        const loadingMsg: AiMessage = {
          id: nextId(),
          role: "assistant",
          content: "",
          timestamp: Date.now(),
          isLoading: true,
        }

        set({ messages: [...messages, userMsg, loadingMsg], isLoading: true, error: null })

        try {
          let content: string
          let actions: AiAction[] = []

          try {
            // Try real API first
            const response = await api.post<{ content: string; actions?: AiAction[] }>("/ai/chat", {
              message: text,
              context,
              history: messages.slice(-10).map((m) => ({ role: m.role, content: m.content })),
            })
            content = response.data.content
            actions = response.data.actions ?? []
          } catch {
            // Fall back to mock response
            const mock = generateMockResponse(text, context)
            content = mock.content
            actions = mock.actions
          }

          const assistantMsg: AiMessage = {
            id: nextId(),
            role: "assistant",
            content,
            timestamp: Date.now(),
            actions,
          }

          set((s) => ({
            messages: s.messages.filter((m) => !m.isLoading).concat(assistantMsg),
            isLoading: false,
          }))
        } catch (err) {
          const errorMsg = err instanceof Error ? err.message : "Ошибка при обращении к AI"
          set((s) => ({
            messages: s.messages.filter((m) => !m.isLoading),
            isLoading: false,
            error: errorMsg,
          }))
        }
      },

      applyAction: async (action: AiAction) => {
        set({ isLoading: true, error: null })
        try {
          switch (action.type) {
            case "create_object":
              await api.post("/designer/objects", action.payload)
              break
            case "write_module":
              await api.put(
                `/designer/objects/${action.payload.objectType}/${action.payload.objectName}/module`,
                action.payload,
              )
              break
            case "add_attribute":
              await api.post(
                `/designer/objects/${action.payload.objectType}/${action.payload.objectName}/attributes`,
                { attributes: action.payload.attributes },
              )
              break
            case "generate_form":
              await api.post(
                `/designer/objects/${action.payload.objectType}/${action.payload.objectName}/forms`,
                { formType: action.payload.formType },
              )
              break
          }

          const confirmMsg: AiMessage = {
            id: nextId(),
            role: "assistant",
            content: `Действие **${action.label}** успешно применено.`,
            timestamp: Date.now(),
          }

          set((s) => ({
            messages: [...s.messages, confirmMsg],
            isLoading: false,
          }))
        } catch (err) {
          const errorMsg = err instanceof Error ? err.message : "Не удалось применить действие"
          set({ isLoading: false, error: errorMsg })
        }
      },
    }),
    {
      name: "oes-ai-assistant",
      partialize: (s) => ({ messages: s.messages.slice(-50), context: s.context }),
    },
  ),
)
