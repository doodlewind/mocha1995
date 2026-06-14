/** Source code editor with debug/error line highlighting. */
import { useEffect, useRef } from 'react'
import CodeMirror from '@uiw/react-codemirror'
import { javascript } from '@codemirror/lang-javascript'
import { EditorView, Decoration, type DecorationSet } from '@codemirror/view'
import { StateField, StateEffect, type EditorState, type Extension, type Range } from '@codemirror/state'
import { Panel } from './Panel'
import { useStore, useCurrentStep } from '../store'

/** Lines to highlight, 1-based. 0/undefined means none. */
interface HighlightLines {
  current: number | undefined
  error: number | undefined
}

/** Effect carrying the new set of lines to highlight. */
const setHighlight = StateEffect.define<HighlightLines>()

const currentLineDeco = Decoration.line({ class: 'cm-current-line-debug' })
const errorLineDeco = Decoration.line({ class: 'cm-error-line' })

/** Build a decoration set for the given lines, guarded against out-of-range. */
function buildDecorations(state: EditorState, lines: HighlightLines): DecorationSet {
  const { doc } = state
  const ranges: Array<Range<Decoration>> = []

  const add = (line: number | undefined, deco: Decoration): void => {
    if (!line || line < 1 || line > doc.lines) return
    const pos = doc.line(line).from
    ranges.push(deco.range(pos))
  }

  // Error first so it sorts before current at the same position if needed.
  add(lines.error, errorLineDeco)
  add(lines.current, currentLineDeco)
  // Decoration.set requires sorted-by-from ranges.
  ranges.sort((a, b) => a.from - b.from)
  return Decoration.set(ranges, true)
}

/** StateField holding the active line decorations. */
const highlightField = StateField.define<DecorationSet>({
  create() {
    return Decoration.none
  },
  update(deco, tr) {
    let next = deco.map(tr.changes)
    for (const effect of tr.effects) {
      if (effect.is(setHighlight)) {
        next = buildDecorations(tr.state, effect.value)
      }
    }
    return next
  },
  provide: (f) => EditorView.decorations.from(f),
})

const extensions: Extension[] = [javascript(), highlightField]

export function EditorPane() {
  const code = useStore((s) => s.code)
  const setCode = useStore((s) => s.setCode)
  const currentLine = useCurrentStep()?.line
  const errorLine = useStore((s) => s.result?.error?.line)

  const viewRef = useRef<EditorView | null>(null)

  // Push highlight lines into the editor whenever they change.
  useEffect(() => {
    const view = viewRef.current
    if (!view) return
    view.dispatch({
      effects: setHighlight.of({ current: currentLine, error: errorLine }),
    })
  }, [currentLine, errorLine])

  return (
    <Panel
      title="Source"
      subtitle="JavaScript 1.1 (Mocha) · edit & Run"
      className="h-full"
      bodyClassName="p-0"
    >
      <div className="h-full min-h-0">
        <CodeMirror
          value={code}
          onChange={(v) => setCode(v)}
          theme="dark"
          height="100%"
          extensions={extensions}
          basicSetup={{
            lineNumbers: true,
            foldGutter: false,
            highlightActiveLine: false,
          }}
          onCreateEditor={(view) => {
            viewRef.current = view
            // Apply any current highlight immediately on mount.
            view.dispatch({
              effects: setHighlight.of({ current: currentLine, error: errorLine }),
            })
          }}
          className="h-full text-[13px]"
          style={{ height: '100%' }}
        />
      </div>
    </Panel>
  )
}
