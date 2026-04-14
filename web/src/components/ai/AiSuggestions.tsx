import { useState } from "react"
import { clsx } from "clsx"
import {
  Lightbulb,
  X,
  Sparkle,
  Code,
  Checks,
  ChartBar,
  Plus,
} from "@phosphor-icons/react"
import { useAiAssistant } from "@/hooks/useAiAssistant"

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type SuggestionTrigger =
  | "empty_module"        // User opens an empty module
  | "new_object"          // User creates a new metadata object
  | "new_attribute"       // User is adding attributes
  | "document_created"    // A document was just created

export interface Suggestion {
  id: string
  label: string
  prompt: string
  icon: React.ReactNode
}

// ---------------------------------------------------------------------------
// Suggestion sets per trigger
// ---------------------------------------------------------------------------

const SUGGESTIONS: Record<SuggestionTrigger, Suggestion[]> = {
  empty_module: [
    {
      id: "gen-handler",
      label: "Generate handler",
      prompt: "Сгенерируй стандартные процедуры ПередЗаписью и ПриЗаписи с валидацией полей",
      icon: <Code size={12} weight="duotone" />,
    },
    {
      id: "add-validation",
      label: "Add validation",
      prompt: "Добавь проверку обязательных реквизитов перед записью объекта",
      icon: <Checks size={12} weight="duotone" />,
    },
    {
      id: "calc-sum",
      label: "Calculate totals",
      prompt: "Напиши функцию расчёта итоговой суммы по табличной части с автоматическим обновлением",
      icon: <Sparkle size={12} weight="duotone" />,
    },
  ],
  new_object: [
    {
      id: "describe-object",
      label: "Describe this object",
      prompt: "Помоги описать что должен делать этот объект и предложи нужные реквизиты",
      icon: <Lightbulb size={12} weight="duotone" />,
    },
    {
      id: "gen-form",
      label: "Generate form",
      prompt: "Сгенерируй схему формы с оптимальным расположением полей",
      icon: <Plus size={12} weight="duotone" />,
    },
    {
      id: "create-report",
      label: "Create report",
      prompt: "Создай отчёт по данным этого объекта с группировкой и итогами",
      icon: <ChartBar size={12} weight="duotone" />,
    },
  ],
  new_attribute: [
    {
      id: "suggest-attrs",
      label: "Suggest attributes",
      prompt: "Предложи типичные реквизиты для данного типа объекта на основе отраслевых стандартов",
      icon: <Sparkle size={12} weight="duotone" />,
    },
    {
      id: "add-standard",
      label: "Add standard set",
      prompt: "Добавь стандартный набор реквизитов: Наименование, Код, Комментарий, Ответственный, Дата",
      icon: <Plus size={12} weight="duotone" />,
    },
  ],
  document_created: [
    {
      id: "write-posting",
      label: "Write posting module",
      prompt: "Напиши модуль проведения с движениями по регистрам для этого документа",
      icon: <Code size={12} weight="duotone" />,
    },
    {
      id: "create-registers",
      label: "Create registers",
      prompt: "Предложи и создай нужные регистры накопления для учёта данных из этого документа",
      icon: <ChartBar size={12} weight="duotone" />,
    },
  ],
}

// ---------------------------------------------------------------------------
// Quick action chip
// ---------------------------------------------------------------------------

interface QuickChipProps {
  suggestion: Suggestion
  onClick: (prompt: string) => void
}

function QuickChip({ suggestion, onClick }: QuickChipProps) {
  return (
    <button
      type="button"
      onClick={() => onClick(suggestion.prompt)}
      className={clsx(
        "flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] font-medium",
        "rounded-[var(--radius)] border border-[hsl(var(--border))]",
        "bg-[hsl(var(--background))] text-[hsl(var(--foreground))]",
        "hover:bg-[hsl(var(--secondary))] hover:border-[hsl(var(--primary)/0.4)]",
        "transition-colors whitespace-nowrap",
      )}
    >
      <span className="text-[hsl(var(--primary))]">{suggestion.icon}</span>
      {suggestion.label}
    </button>
  )
}

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

interface AiSuggestionsProps {
  trigger: SuggestionTrigger
  objectName?: string
  onDismiss?: () => void
  className?: string
}

export function AiSuggestions({ trigger, objectName, onDismiss, className }: AiSuggestionsProps) {
  const { open, sendMessage } = useAiAssistant()
  const [dismissed, setDismissed] = useState(false)

  if (dismissed) return null

  const suggestions = SUGGESTIONS[trigger] ?? []

  const handleSuggestion = async (prompt: string) => {
    const fullPrompt = objectName ? `${prompt} (объект: ${objectName})` : prompt
    open()
    await sendMessage(fullPrompt)
    handleDismiss()
  }

  const handleDismiss = () => {
    setDismissed(true)
    onDismiss?.()
  }

  const TRIGGER_LABELS: Record<SuggestionTrigger, string> = {
    empty_module: "Модуль пустой — хотите сгенерировать код?",
    new_object: "Опишите что должен делать этот объект",
    new_attribute: "AI может предложить реквизиты",
    document_created: "Документ создан — настроить проведение?",
  }

  return (
    <div
      className={clsx(
        "flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius)]",
        "border border-[hsl(var(--primary)/0.2)] bg-[hsl(var(--primary)/0.04)]",
        className,
      )}
    >
      <Lightbulb
        size={16}
        weight="duotone"
        className="shrink-0 mt-0.5 text-[hsl(var(--primary))]"
      />

      <div className="flex-1 min-w-0">
        <p className="text-[11px] font-medium text-[hsl(var(--foreground))] mb-2">
          {TRIGGER_LABELS[trigger]}
        </p>
        <div className="flex flex-wrap gap-1.5">
          {suggestions.map((s) => (
            <QuickChip
              key={s.id}
              suggestion={s}
              onClick={(prompt) => void handleSuggestion(prompt)}
            />
          ))}
        </div>
      </div>

      <button
        type="button"
        onClick={handleDismiss}
        className="shrink-0 p-0.5 rounded hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))]"
        title="Dismiss"
      >
        <X size={13} />
      </button>
    </div>
  )
}
