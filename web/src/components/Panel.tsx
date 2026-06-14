/** Shared titled panel container used by every visualization pane. */
import type { ReactNode } from 'react'
import clsx from 'clsx'

interface PanelProps {
  title: string
  /** Short hint shown under the title. */
  subtitle?: string
  /** Right-aligned controls in the header. */
  actions?: ReactNode
  /** Small count/badge shown next to the title. */
  badge?: ReactNode
  className?: string
  bodyClassName?: string
  children: ReactNode
}

export function Panel({
  title,
  subtitle,
  actions,
  badge,
  className,
  bodyClassName,
  children,
}: PanelProps) {
  return (
    <section
      className={clsx(
        'flex min-h-0 flex-col overflow-hidden rounded-xl border border-ink-600 bg-ink-800/60 backdrop-blur',
        className,
      )}
    >
      <header className="flex items-center justify-between gap-2 border-b border-ink-600 bg-ink-700/50 px-3 py-2">
        <div className="min-w-0">
          <div className="flex items-center gap-2">
            <h2 className="truncate text-sm font-semibold tracking-wide text-ink-100">
              {title}
            </h2>
            {badge}
          </div>
          {subtitle && (
            <p className="truncate text-[11px] text-ink-300">{subtitle}</p>
          )}
        </div>
        {actions && <div className="flex shrink-0 items-center gap-1">{actions}</div>}
      </header>
      <div className={clsx('min-h-0 flex-1 overflow-auto', bodyClassName)}>
        {children}
      </div>
    </section>
  )
}

/** A small rounded count badge. */
export function CountBadge({ children }: { children: ReactNode }) {
  return (
    <span className="rounded-full bg-ink-600 px-2 py-0.5 text-[10px] font-medium text-ink-200">
      {children}
    </span>
  )
}

/** Empty-state placeholder for panels with no data yet. */
export function EmptyState({ children }: { children: ReactNode }) {
  return (
    <div className="flex h-full items-center justify-center p-6 text-center text-sm text-ink-400">
      {children}
    </div>
  )
}
