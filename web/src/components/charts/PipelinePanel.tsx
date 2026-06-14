/**
 * Compiler-pipeline diagram: visualises how Mocha turns source text into
 * results, with a live metric on each stage that updates after every run.
 */
import { useMemo } from 'react'
import clsx from 'clsx'
import { useStore } from '../../store'
import type { RunResult } from '../../engine/types'
import { Panel } from '../Panel'

type StageKey =
  | 'source'
  | 'scanner'
  | 'tokens'
  | 'parser'
  | 'bytecode'
  | 'interpreter'
  | 'output'

interface Stage {
  key: StageKey
  icon: string
  title: string
  /** Primary live metric, big. */
  metric: string
  /** Optional secondary line under the metric. */
  detail?: string
  /** Accent stages carry data; muted stages are pure transforms. */
  kind: 'data' | 'transform'
}

/** Count source lines (at least 1 for a non-empty string). */
function countLines(code: string): number {
  if (code.length === 0) return 0
  return code.split('\n').length
}

/** Count printed output lines, ignoring a single trailing newline. */
function countOutputLines(output: string): number {
  if (output.length === 0) return 0
  const trimmed = output.endsWith('\n') ? output.slice(0, -1) : output
  return trimmed.length === 0 ? 0 : trimmed.split('\n').length
}

function buildStages(code: string, result: RunResult | null): Stage[] {
  const lines = countLines(code)
  const tokenCount = result?.tokens.length ?? 0
  const scripts = result?.scripts ?? []
  const instrCount = scripts.reduce((sum, s) => sum + s.bytecode.length, 0)
  const steps = result?.stats.steps ?? 0
  const maxDepth = result?.trace.reduce((m, t) => Math.max(m, t.depth), 0) ?? 0
  const outLines = result ? countOutputLines(result.output) : 0

  return [
    {
      key: 'source',
      icon: '📄',
      title: 'Source',
      metric: `${lines}`,
      detail: lines === 1 ? 'line' : 'lines',
      kind: 'data',
    },
    {
      key: 'scanner',
      icon: '🔍',
      title: 'Scanner',
      metric: 'lex',
      detail: 'text → tokens',
      kind: 'transform',
    },
    {
      key: 'tokens',
      icon: '🔤',
      title: 'Tokens',
      metric: `${tokenCount}`,
      detail: 'tokens',
      kind: 'data',
    },
    {
      key: 'parser',
      icon: '🌳',
      title: 'Parser / Emitter',
      metric: 'compile',
      detail: 'tokens → ops',
      kind: 'transform',
    },
    {
      key: 'bytecode',
      icon: '🧱',
      title: 'Bytecode',
      metric: `${instrCount}`,
      detail: `${scripts.length} ${scripts.length === 1 ? 'script' : 'scripts'}`,
      kind: 'data',
    },
    {
      key: 'interpreter',
      icon: '⚙️',
      title: 'Interpreter',
      metric: `${steps}`,
      detail: `steps · depth ${maxDepth}`,
      kind: 'transform',
    },
    {
      key: 'output',
      icon: '📤',
      title: 'Output',
      metric: `${outLines}`,
      detail: outLines === 1 ? 'line' : 'lines',
      kind: 'data',
    },
  ]
}

/** Which stage is currently "in flight" while a run is executing. */
const RUNNING_STAGE: StageKey = 'interpreter'

interface StageCardProps {
  stage: Stage
  active: boolean
  hasResult: boolean
}

function StageCard({ stage, active, hasResult }: StageCardProps) {
  const isData = stage.kind === 'data'
  return (
    <div
      className={clsx(
        'relative flex w-full flex-col items-center gap-1 rounded-lg border px-3 py-3 text-center transition-colors sm:w-28',
        active
          ? 'animate-pulse border-accent bg-accent/15'
          : isData
            ? 'border-mocha-700/60 bg-mocha-900/40'
            : 'border-ink-600 bg-ink-700/40',
      )}
    >
      <span className="text-xl leading-none" aria-hidden>
        {stage.icon}
      </span>
      <span className="text-[11px] font-semibold tracking-wide text-ink-100">
        {stage.title}
      </span>
      <span
        className={clsx(
          'font-mono text-lg font-bold leading-none',
          isData
            ? hasResult
              ? 'text-accent-bright'
              : 'text-ink-400'
            : 'text-mocha-200',
        )}
      >
        {stage.metric}
      </span>
      {stage.detail && (
        <span className="text-[10px] leading-tight text-ink-400">
          {stage.detail}
        </span>
      )}
    </div>
  )
}

/** Directional connector between stage cards. */
function Arrow() {
  return (
    <div className="flex shrink-0 items-center justify-center text-ink-500">
      {/* horizontal on >= sm, vertical (rotated) when stages wrap */}
      <span className="hidden sm:inline" aria-hidden>
        →
      </span>
      <span className="sm:hidden" aria-hidden>
        ↓
      </span>
    </div>
  )
}

export function PipelinePanel() {
  const code = useStore((s) => s.code)
  const result = useStore((s) => s.result)
  const status = useStore((s) => s.status)

  const stages = useMemo(() => buildStages(code, result), [code, result])
  const running = status === 'running'
  const hasResult = result !== null

  return (
    <Panel
      title="Engine pipeline"
      subtitle="how Mocha turns source into results"
    >
      <div className="flex min-h-0 flex-1 flex-col p-3">
        <div className="flex flex-col flex-wrap items-stretch justify-center gap-1 sm:flex-row sm:items-center">
          {stages.map((stage, i) => (
            <div
              key={stage.key}
              className="flex flex-col items-center gap-1 sm:flex-row"
            >
              <StageCard
                stage={stage}
                active={running && stage.key === RUNNING_STAGE}
                hasResult={hasResult}
              />
              {i < stages.length - 1 && <Arrow />}
            </div>
          ))}
        </div>
        <p className="mt-3 text-center text-[11px] text-ink-400">
          {hasResult
            ? `Mocha scanned ${result?.tokens.length ?? 0} tokens and ran ${
                result?.stats.steps ?? 0
              } bytecode steps.`
            : 'Run a program to watch source flow through every stage.'}
        </p>
      </div>
    </Panel>
  )
}
