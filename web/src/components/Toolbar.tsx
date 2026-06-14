/** Top application bar: wordmark, examples picker, Run button, status pill. */
import { useEffect, type ChangeEvent } from 'react'
import clsx from 'clsx'
import { useStore, type RunStatus } from '../store'
import { EXAMPLES } from '../engine/examples'

const STATUS_STYLE: Record<RunStatus, { dot: string; chip: string; label: string }> = {
  idle: {
    dot: 'bg-ink-400',
    chip: 'bg-ink-700 text-ink-300 border-ink-600',
    label: 'Idle',
  },
  running: {
    dot: 'bg-accent animate-pulse',
    chip: 'bg-accent/15 text-accent-bright border-accent/40',
    label: 'Running',
  },
  done: {
    dot: 'bg-emerald-400',
    chip: 'bg-emerald-500/15 text-emerald-300 border-emerald-500/40',
    label: 'Done',
  },
  error: {
    dot: 'bg-red-400',
    chip: 'bg-red-500/15 text-red-300 border-red-500/40',
    label: 'Error',
  },
}

function Spinner() {
  return (
    <svg
      className="h-3.5 w-3.5 animate-spin text-current"
      viewBox="0 0 24 24"
      fill="none"
      aria-hidden="true"
    >
      <circle
        className="opacity-25"
        cx="12"
        cy="12"
        r="10"
        stroke="currentColor"
        strokeWidth="4"
      />
      <path
        className="opacity-75"
        fill="currentColor"
        d="M4 12a8 8 0 0 1 8-8V0C5.4 0 0 5.4 0 12h4z"
      />
    </svg>
  )
}

export function Toolbar() {
  const status = useStore((s) => s.status)
  const result = useStore((s) => s.result)
  const setCode = useStore((s) => s.setCode)
  const run = useStore((s) => s.run)

  const isRunning = status === 'running'

  // Cmd/Ctrl+Enter runs the current source.
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') {
        e.preventDefault()
        if (useStore.getState().status !== 'running') void useStore.getState().run()
      }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [])

  const onPickExample = (e: ChangeEvent<HTMLSelectElement>) => {
    const ex = EXAMPLES.find((x) => x.id === e.target.value)
    if (!ex) return
    setCode(ex.code)
    void run()
    e.target.selectedIndex = 0
  }

  const statusStyle = STATUS_STYLE[status]

  return (
    <header className="flex items-center gap-3 border-b border-ink-600 bg-ink-800 px-4 py-2.5">
      {/* Wordmark */}
      <div className="flex min-w-0 shrink-0 flex-col leading-none">
        <span className="text-base font-bold tracking-tight text-accent">
          ☕ Mocha 1995
        </span>
        <span className="mt-0.5 text-[10px] uppercase tracking-wide text-ink-400">
          Engine Playground &amp; Debugger
        </span>
      </div>

      <div className="ml-2 hidden h-6 w-px shrink-0 bg-ink-600 sm:block" />

      {/* Examples dropdown */}
      <div className="relative shrink-0">
        <select
          aria-label="Load an example program"
          defaultValue=""
          onChange={onPickExample}
          disabled={isRunning}
          className="appearance-none rounded-lg border border-ink-600 bg-ink-700 py-1.5 pl-3 pr-8 text-xs font-medium text-ink-100 transition-colors hover:border-ink-500 focus:border-accent focus:outline-none disabled:cursor-not-allowed disabled:opacity-50"
        >
          <option value="" disabled>
            Examples…
          </option>
          {EXAMPLES.map((ex) => (
            <option key={ex.id} value={ex.id} className="bg-ink-800 text-ink-100">
              {ex.title}
            </option>
          ))}
        </select>
        <span className="pointer-events-none absolute right-2.5 top-1/2 -translate-y-1/2 text-[10px] text-ink-400">
          ▼
        </span>
      </div>

      {/* Run button */}
      <button
        type="button"
        onClick={() => void run()}
        disabled={isRunning}
        title="Run (⌘/Ctrl + Enter)"
        className={clsx(
          'inline-flex shrink-0 items-center gap-2 rounded-lg px-4 py-1.5 text-xs font-semibold transition-colors',
          isRunning
            ? 'cursor-not-allowed bg-mocha-700 text-mocha-200'
            : 'bg-accent text-ink-900 hover:bg-accent-bright',
        )}
      >
        {isRunning ? (
          <>
            <Spinner />
            Running…
          </>
        ) : (
          <>Run ▶</>
        )}
      </button>

      {/* Status pill */}
      <div
        className={clsx(
          'inline-flex shrink-0 items-center gap-2 rounded-full border px-3 py-1 text-[11px] font-medium',
          statusStyle.chip,
        )}
      >
        <span className={clsx('h-1.5 w-1.5 rounded-full', statusStyle.dot)} />
        <span>{statusStyle.label}</span>
        {result && (
          <span className="font-mono text-current/80">
            {result.stats.steps.toLocaleString()} steps
          </span>
        )}
        {result?.truncated && (
          <span
            title="Trace was truncated — increase the step limit to see more"
            className="rounded bg-amber-500/20 px-1.5 py-0.5 text-[10px] font-semibold text-amber-300"
          >
            truncated
          </span>
        )}
      </div>

      <div className="ml-auto" />

      {/* GitHub link */}
      <a
        href="https://github.com/doodlewind/mocha1995"
        target="_blank"
        rel="noopener noreferrer"
        title="View source on GitHub"
        className="shrink-0 rounded-lg p-1.5 text-ink-400 transition-colors hover:bg-ink-700 hover:text-ink-100"
      >
        <svg
          className="h-5 w-5"
          viewBox="0 0 16 16"
          fill="currentColor"
          aria-hidden="true"
        >
          <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
        </svg>
      </a>
    </header>
  )
}
