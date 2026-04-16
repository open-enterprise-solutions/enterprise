/**
 * FormSkeleton — shown while schema or initial data is loading.
 * Uses the .skeleton CSS class from globals.css (shimmer animation).
 */

export function FormSkeleton() {
  return (
    <div className="flex flex-col h-full">
      {/* Command bar placeholder */}
      <div className="flex items-center gap-2 px-4 py-2 border-b border-[hsl(var(--border))] bg-[hsl(var(--secondary)/0.4)]">
        <div className="skeleton h-8 w-20 rounded-[var(--radius)]" />
        <div className="skeleton h-8 w-16 rounded-[var(--radius)]" />
        <div className="skeleton h-8 w-16 rounded-[var(--radius)]" />
      </div>

      {/* Form body */}
      <div className="flex-1 overflow-auto p-4 flex flex-col gap-3">
        {/* Header */}
        <div className="skeleton h-5 w-48 rounded-[var(--radius)] mb-2" />

        {/* Field rows */}
        {Array.from({ length: 6 }).map((_, i) => (
          <div key={i} className="flex flex-col gap-1">
            <div className="skeleton h-3 w-24 rounded-[var(--radius)]" />
            <div className="skeleton h-8 w-full rounded-[var(--radius)]" />
          </div>
        ))}

        {/* Tabular section placeholder */}
        <div className="mt-4">
          <div className="skeleton h-3 w-32 rounded-[var(--radius)] mb-2" />
          <div className="skeleton h-40 w-full rounded-[var(--radius)]" />
        </div>
      </div>
    </div>
  )
}
