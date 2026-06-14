/**
 * Execution timeline chart: value-stack size (filled area, accent) and call
 * depth (line, violet) plotted per trace step. Interactive — click/drag to
 * scrub the current step. Downsamples the drawn paths for very long traces
 * while keeping click mapping at full resolution.
 */
import { useCallback, useLayoutEffect, useMemo, useRef, useState } from 'react'
import { Panel, EmptyState } from '../Panel'
import { useStore } from '../../store'

const DEPTH_COLOR = '#c490e4'
const ACCENT = '#e0954a'
const MAX_POINTS = 1000

/** Pad applied inside the SVG viewBox so markers/labels do not clip. */
const PAD = { top: 8, right: 8, bottom: 18, left: 8 }
const VIEW_W = 1000
const VIEW_H = 240
const PLOT_W = VIEW_W - PAD.left - PAD.right
const PLOT_H = VIEW_H - PAD.top - PAD.bottom

interface Series {
  /** Per-step value-stack size. */
  stack: number[]
  /** Per-step call depth. */
  depth: number[]
  maxStack: number
  maxDepth: number
}

/** Map a step index (0..n-1) to an x coordinate within the plot area. */
function stepToX(index: number, n: number): number {
  if (n <= 1) return PAD.left + PLOT_W / 2
  return PAD.left + (index / (n - 1)) * PLOT_W
}

/** Map a value 0..max to a y coordinate (inverted: 0 at bottom). */
function valToY(value: number, max: number): number {
  const frac = max <= 0 ? 0 : value / max
  return PAD.top + PLOT_H - frac * PLOT_H
}

/** Indices to actually draw, downsampled to ~MAX_POINTS for long traces. */
function sampleIndices(n: number): number[] {
  if (n <= MAX_POINTS) {
    const all: number[] = []
    for (let i = 0; i < n; i++) all.push(i)
    return all
  }
  const out: number[] = []
  const stride = (n - 1) / (MAX_POINTS - 1)
  for (let i = 0; i < MAX_POINTS; i++) out.push(Math.round(i * stride))
  // Guarantee the final point is included.
  if (out[out.length - 1] !== n - 1) out.push(n - 1)
  return out
}

export function StackDepthChart() {
  const result = useStore((s) => s.result)
  const stepIndex = useStore((s) => s.stepIndex)
  const setStep = useStore((s) => s.setStep)
  const trace = result?.trace ?? []
  const n = trace.length

  const svgRef = useRef<SVGSVGElement | null>(null)
  const draggingRef = useRef(false)
  const [hover, setHover] = useState<number | null>(null)

  const series = useMemo<Series>(() => {
    const stack: number[] = []
    const depth: number[] = []
    let maxStack = 0
    let maxDepth = 0
    for (const step of trace) {
      const s = step.stack.length
      const d = step.depth
      stack.push(s)
      depth.push(d)
      if (s > maxStack) maxStack = s
      if (d > maxDepth) maxDepth = d
    }
    return { stack, depth, maxStack, maxDepth }
  }, [trace])

  const { stackArea, depthPath } = useMemo(() => {
    if (n === 0) return { stackArea: '', depthPath: '' }
    const idx = sampleIndices(n)
    const yMaxStack = Math.max(1, series.maxStack)
    const yMaxDepth = Math.max(1, series.maxDepth)

    let area = ''
    let line = ''
    idx.forEach((i, k) => {
      const x = stepToX(i, n)
      const ys = valToY(series.stack[i], yMaxStack)
      const yd = valToY(series.depth[i], yMaxDepth)
      area += `${k === 0 ? 'M' : 'L'}${x.toFixed(2)},${ys.toFixed(2)} `
      line += `${k === 0 ? 'M' : 'L'}${x.toFixed(2)},${yd.toFixed(2)} `
    })
    // Close the area path down to the baseline.
    const lastX = stepToX(idx[idx.length - 1], n)
    const firstX = stepToX(idx[0], n)
    const baseY = PAD.top + PLOT_H
    area += `L${lastX.toFixed(2)},${baseY.toFixed(2)} L${firstX.toFixed(2)},${baseY.toFixed(2)} Z`
    return { stackArea: area.trim(), depthPath: line.trim() }
  }, [n, series])

  /** Convert a pointer event to a step index over the full (non-sampled) range. */
  const eventToStep = useCallback(
    (clientX: number): number | null => {
      const svg = svgRef.current
      if (!svg || n === 0) return null
      const rect = svg.getBoundingClientRect()
      if (rect.width === 0) return null
      // Translate client px -> viewBox units -> plot fraction.
      const vbX = ((clientX - rect.left) / rect.width) * VIEW_W
      const frac = (vbX - PAD.left) / PLOT_W
      const clamped = Math.min(1, Math.max(0, frac))
      return Math.round(clamped * (n - 1))
    },
    [n],
  )

  const handlePointerDown = useCallback(
    (e: React.PointerEvent<SVGSVGElement>) => {
      const i = eventToStep(e.clientX)
      if (i === null) return
      draggingRef.current = true
      e.currentTarget.setPointerCapture(e.pointerId)
      setHover(i)
      setStep(i)
    },
    [eventToStep, setStep],
  )

  const handlePointerMove = useCallback(
    (e: React.PointerEvent<SVGSVGElement>) => {
      const i = eventToStep(e.clientX)
      if (i === null) return
      setHover(i)
      if (draggingRef.current) setStep(i)
    },
    [eventToStep, setStep],
  )

  const handlePointerUp = useCallback(
    (e: React.PointerEvent<SVGSVGElement>) => {
      draggingRef.current = false
      if (e.currentTarget.hasPointerCapture(e.pointerId)) {
        e.currentTarget.releasePointerCapture(e.pointerId)
      }
    },
    [],
  )

  // Keep the hover readout in sync with the current step when not interacting.
  useLayoutEffect(() => {
    if (!draggingRef.current) setHover(null)
  }, [stepIndex])

  if (n === 0) {
    return (
      <Panel
        title="Execution timeline"
        subtitle="stack size & call depth per step"
      >
        <EmptyState>Run a script to see how the stack grows over time.</EmptyState>
      </Panel>
    )
  }

  const readoutIndex = hover ?? (stepIndex >= 0 && stepIndex < n ? stepIndex : null)
  const readout = readoutIndex !== null ? trace[readoutIndex] : null

  const markerIndex = stepIndex >= 0 && stepIndex < n ? stepIndex : null
  const markerX = markerIndex !== null ? stepToX(markerIndex, n) : null
  const hoverX = hover !== null ? stepToX(hover, n) : null

  // Horizontal gridlines at quarter intervals.
  const gridY = [0.25, 0.5, 0.75].map((f) => PAD.top + PLOT_H - f * PLOT_H)

  return (
    <Panel
      title="Execution timeline"
      subtitle="stack size & call depth per step"
      actions={
        <div className="flex items-center gap-3 text-[10px] text-ink-300">
          <span className="flex items-center gap-1">
            <span
              className="inline-block h-2 w-2 rounded-sm"
              style={{ backgroundColor: ACCENT }}
            />
            stack
          </span>
          <span className="flex items-center gap-1">
            <span
              className="inline-block h-2 w-3 rounded-sm"
              style={{ backgroundColor: DEPTH_COLOR }}
            />
            depth
          </span>
        </div>
      }
      bodyClassName="flex flex-col"
    >
      <div className="flex min-h-0 flex-1 flex-col p-3">
        <div className="relative min-h-0 flex-1">
          <svg
            ref={svgRef}
            viewBox={`0 0 ${VIEW_W} ${VIEW_H}`}
            preserveAspectRatio="none"
            className="h-full w-full cursor-crosshair touch-none select-none"
            role="slider"
            aria-label="Execution step"
            aria-valuemin={0}
            aria-valuemax={n - 1}
            aria-valuenow={markerIndex ?? 0}
            onPointerDown={handlePointerDown}
            onPointerMove={handlePointerMove}
            onPointerUp={handlePointerUp}
            onPointerLeave={handlePointerUp}
          >
            {/* baseline + gridlines */}
            {gridY.map((y, i) => (
              <line
                key={i}
                x1={PAD.left}
                x2={PAD.left + PLOT_W}
                y1={y}
                y2={y}
                stroke="#ffffff"
                strokeOpacity={0.05}
                strokeWidth={1}
                vectorEffect="non-scaling-stroke"
              />
            ))}
            <line
              x1={PAD.left}
              x2={PAD.left + PLOT_W}
              y1={PAD.top + PLOT_H}
              y2={PAD.top + PLOT_H}
              stroke="#ffffff"
              strokeOpacity={0.12}
              strokeWidth={1}
              vectorEffect="non-scaling-stroke"
            />

            {/* stack area */}
            <path d={stackArea} fill={ACCENT} fillOpacity={0.16} />
            <path
              d={stackArea}
              fill="none"
              stroke={ACCENT}
              strokeOpacity={0.55}
              strokeWidth={1.5}
              strokeLinejoin="round"
              vectorEffect="non-scaling-stroke"
            />

            {/* depth line */}
            <path
              d={depthPath}
              fill="none"
              stroke={DEPTH_COLOR}
              strokeWidth={1.5}
              strokeLinejoin="round"
              strokeLinecap="round"
              vectorEffect="non-scaling-stroke"
            />

            {/* hover marker (faint) */}
            {hoverX !== null && hoverX !== markerX && (
              <line
                x1={hoverX}
                x2={hoverX}
                y1={PAD.top}
                y2={PAD.top + PLOT_H}
                stroke="#ffffff"
                strokeOpacity={0.18}
                strokeWidth={1}
                vectorEffect="non-scaling-stroke"
              />
            )}

            {/* current-step marker */}
            {markerX !== null && (
              <line
                x1={markerX}
                x2={markerX}
                y1={PAD.top}
                y2={PAD.top + PLOT_H}
                stroke={ACCENT}
                strokeWidth={1.5}
                vectorEffect="non-scaling-stroke"
              />
            )}
          </svg>

          {/* axis max labels (overlaid, non-distorting) */}
          <div className="pointer-events-none absolute left-1 top-1 font-mono text-[10px] leading-none text-ink-400">
            stack max {series.maxStack}
          </div>
          <div className="pointer-events-none absolute right-1 top-1 font-mono text-[10px] leading-none text-ink-400">
            depth max {series.maxDepth}
          </div>
          <div className="pointer-events-none absolute bottom-1 left-1 font-mono text-[10px] leading-none text-ink-400">
            step 0
          </div>
          <div className="pointer-events-none absolute bottom-1 right-1 font-mono text-[10px] leading-none text-ink-400">
            step {n - 1}
          </div>
        </div>

        {/* readout */}
        <div className="mt-2 flex shrink-0 items-center gap-4 border-t border-ink-600 pt-2 font-mono text-[11px]">
          {readout ? (
            <>
              <span className="text-ink-200">
                step <span className="text-ink-100">{readout.step}</span>
              </span>
              <span style={{ color: ACCENT }}>
                stack {readout.stack.length}
              </span>
              <span style={{ color: DEPTH_COLOR }}>depth {readout.depth}</span>
              <span className="truncate text-ink-400">{readout.op}</span>
            </>
          ) : (
            <span className="text-ink-400">hover or drag to inspect a step</span>
          )}
        </div>
      </div>
    </Panel>
  )
}
