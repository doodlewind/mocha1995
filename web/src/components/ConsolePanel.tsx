/**
 * Console output panel: shows print() output, the final result value,
 * parse/runtime errors with a caret under the offending column, and run stats.
 */
import { useStore } from '../store'
import { datumColor } from '../theme'
import type { MochaError } from '../engine/types'
import { Panel, CountBadge, EmptyState } from './Panel'

/** Renders a Mocha error block with the source line and a caret marker. */
function ErrorBlock({ error }: { error: MochaError }) {
  // error.col is 1-based; clamp so the caret never goes negative.
  const caretPad = Math.max(0, error.col - 1)
  const caret = `${' '.repeat(caretPad)}^`
  return (
    <div className="rounded-lg border border-red-500/40 bg-red-500/10 p-3 text-red-200">
      <div className="flex items-baseline gap-2">
        <span className="text-xs font-semibold uppercase tracking-wide text-red-300">
          Error
        </span>
        <span className="text-[11px] text-red-300/80">
          line {error.line}:{error.col}
        </span>
      </div>
      <p className="mt-1 text-sm font-medium text-red-100">{error.message}</p>
      {error.linebuf !== '' && (
        <pre className="mt-2 overflow-x-auto whitespace-pre font-mono text-xs leading-tight text-red-200/90">
          {error.linebuf}
          {'\n'}
          <span className="text-accent">{caret}</span>
        </pre>
      )}
    </div>
  )
}

export function ConsolePanel() {
  const result = useStore((s) => s.result)
  const status = useStore((s) => s.status)
  const loadError = useStore((s) => s.loadError)

  if (!result && status === 'idle' && !loadError) {
    return (
      <Panel title="Console" subtitle="print() output & result">
        <EmptyState>Press Run to execute with the 1995 engine.</EmptyState>
      </Panel>
    )
  }

  const output = result?.output ?? ''
  const finalValue = result?.result ?? null
  const stats = result?.stats ?? null

  return (
    <Panel
      title="Console"
      subtitle="print() output & result"
      badge={
        result?.truncated ? (
          <span className="rounded-full border border-amber-500/40 bg-amber-500/15 px-2 py-0.5 text-[10px] font-medium text-amber-200">
            trace truncated
          </span>
        ) : undefined
      }
    >
      <div className="flex min-h-0 flex-col gap-3 p-3">
        {/* print() output */}
        {output !== '' ? (
          <pre className="overflow-x-auto whitespace-pre-wrap break-words font-mono text-xs leading-relaxed text-ink-100">
            {output}
          </pre>
        ) : (
          <p className="font-mono text-xs italic text-ink-400">(no output)</p>
        )}

        {/* engine load/run failure */}
        {loadError && (
          <div className="rounded-lg border border-red-500/40 bg-red-500/10 p-3 text-red-200">
            <div className="text-xs font-semibold uppercase tracking-wide text-red-300">
              Engine error
            </div>
            <p className="mt-1 whitespace-pre-wrap break-words text-sm font-medium text-red-100">
              {loadError}
            </p>
          </div>
        )}

        {/* parse / runtime error */}
        {result?.error && <ErrorBlock error={result.error} />}

        {/* final value */}
        {finalValue && (
          <div className="flex items-center gap-2 rounded-lg border border-ink-600 bg-ink-700/40 px-3 py-2 font-mono text-xs">
            <span className="text-ink-400">⇒</span>
            <span
              className="inline-block h-2.5 w-2.5 shrink-0 rounded-full"
              style={{ backgroundColor: datumColor(finalValue.type) }}
              aria-hidden
            />
            <span className="min-w-0 break-all text-ink-100">
              {finalValue.value}
            </span>
            <span className="ml-auto shrink-0 text-ink-400">
              : {finalValue.type}
            </span>
          </div>
        )}

        {/* run stats */}
        {stats && (
          <div className="flex flex-wrap items-center gap-2 text-[11px] text-ink-300">
            <span className="flex items-center gap-1">
              <CountBadge>{stats.steps}</CountBadge> steps
            </span>
            <span className="flex items-center gap-1">
              <CountBadge>{stats.scripts}</CountBadge>{' '}
              {stats.scripts === 1 ? 'script' : 'scripts'}
            </span>
          </div>
        )}
      </div>
    </Panel>
  )
}
