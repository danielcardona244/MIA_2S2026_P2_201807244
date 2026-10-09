import { useRef, useState } from 'react'

const INITIAL_SCRIPT = `# ExtreamFS
# Escribe comandos aqui o carga un archivo .smia
`

export default function App() {
  const [commands, setCommands] = useState(INITIAL_SCRIPT)
  const [output, setOutput] = useState('')
  const [running, setRunning] = useState(false)
  const [status, setStatus] = useState('Listo')
  const [journalId, setJournalId] = useState('')
  const [journalEntries, setJournalEntries] = useState([])
  const [journalError, setJournalError] = useState('')
  const [journalLoading, setJournalLoading] = useState(false)
  const fileInput = useRef(null)

  const loadScript = async (event) => {
    const file = event.target.files?.[0]
    if (!file) return

    if (!file.name.toLowerCase().endsWith('.smia')) {
      setStatus('El script debe tener extension .smia')
      event.target.value = ''
      return
    }

    try {
      const text = await file.text()
      setCommands(text)
      setStatus(`Script cargado: ${file.name}`)
    } catch {
      setStatus('No se pudo leer el archivo seleccionado')
    } finally {
      event.target.value = ''
    }
  }

  const execute = async () => {
    if (!commands.trim()) {
      setStatus('No hay comandos para ejecutar')
      return
    }

    const executableLines = commands
      .split('\n')
      .map((line) => line.trim())
      .filter((line) => line && !line.startsWith('#'))

    const containsFdiskDelete = executableLines.some(
      (line) => /\bfdisk\b/i.test(line) && /-delete\s*=/i.test(line),
    )
    const containsLoss = executableLines.some((line) => /^loss\b/i.test(line))

    if (containsFdiskDelete) {
      const confirmed = window.confirm(
        'El script contiene FDISK -delete. Esta operacion puede eliminar una particion y sus datos. ¿Desea continuar?',
      )
      if (!confirmed) {
        setStatus('Eliminacion cancelada por el usuario')
        return
      }
    }

    if (containsLoss) {
      const confirmed = window.confirm(
        'El script contiene LOSS. Se limpiaran bitmaps, inodos y bloques de la particion EXT3 indicada. ¿Desea continuar?',
      )
      if (!confirmed) {
        setStatus('Simulacion LOSS cancelada por el usuario')
        return
      }
    }

    setRunning(true)
    setStatus('Ejecutando...')

    try {
      const response = await fetch('/api/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ commands }),
      })

      const data = await response.json()
      if (!response.ok || !data.ok) {
        throw new Error(data.error || `HTTP ${response.status}`)
      }

      setOutput(data.output || '')
      setStatus('Ejecucion completada')
    } catch (error) {
      setOutput(`[ERROR] FRONTEND: ${error.message}`)
      setStatus('Error al comunicarse con el backend')
    } finally {
      setRunning(false)
    }
  }

  const loadJournal = async () => {
    const id = journalId.trim()
    if (!id) {
      setJournalError('Ingrese el id de una particion EXT3 montada')
      setJournalEntries([])
      return
    }

    setJournalLoading(true)
    setJournalError('')
    try {
      const response = await fetch('/api/journaling', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id }),
      })
      const data = await response.json()
      if (!response.ok || !data.ok) {
        throw new Error(data.error || `HTTP ${response.status}`)
      }
      setJournalEntries(Array.isArray(data.entries) ? data.entries : [])
    } catch (error) {
      setJournalEntries([])
      setJournalError(error.message)
    } finally {
      setJournalLoading(false)
    }
  }

  const clearAll = () => {
    setCommands('')
    setOutput('')
    setStatus('Limpio')
  }

  return (
    <main className="app-shell">
      <header className="hero">
        <div>
          <span className="eyebrow">MIA · Proyecto 2</span>
          <h1>ExtreamFS</h1>
          <p>Interfaz web para ejecutar comandos sobre sistemas de archivos EXT2 y EXT3.</p>
        </div>
        <div className="status-pill" aria-live="polite">
          <span className={running ? 'dot busy' : 'dot'} />
          {status}
        </div>
      </header>

      <section className="panel">
        <div className="panel-heading">
          <div>
            <h2>Entrada de comandos</h2>
            <p>Los comandos y parametros no distinguen mayusculas de minusculas.</p>
          </div>

          <div className="actions">
            <input
              ref={fileInput}
              className="visually-hidden"
              type="file"
              accept=".smia,text/plain"
              onChange={loadScript}
            />
            <button
              className="button secondary"
              type="button"
              onClick={() => fileInput.current?.click()}
              disabled={running}
            >
              Cargar .smia
            </button>
            <button
              className="button primary"
              type="button"
              onClick={execute}
              disabled={running}
            >
              {running ? 'Ejecutando...' : 'Ejecutar'}
            </button>
            <button
              className="button ghost"
              type="button"
              onClick={clearAll}
              disabled={running}
            >
              Limpiar
            </button>
          </div>
        </div>

        <textarea
          className="terminal input"
          aria-label="Entrada de comandos"
          spellCheck="false"
          value={commands}
          onChange={(event) => setCommands(event.target.value)}
          placeholder="mkdisk -size=10 -unit=M -path=/tmp/disco.mia"
        />
      </section>

      <section className="panel">
        <div className="panel-heading">
          <div>
            <h2>Salida del servidor</h2>
            <p>Resultado devuelto por el backend C++ mediante la API REST.</p>
          </div>
        </div>

        <textarea
          className="terminal output"
          aria-label="Salida de comandos"
          readOnly
          value={output}
          placeholder="La salida de la ejecucion aparecera aqui..."
        />
      </section>

      <section className="panel journal-panel">
        <div className="panel-heading">
          <div>
            <h2>Journaling EXT3</h2>
            <p>Visualiza las transacciones persistidas en una particion EXT3 montada.</p>
          </div>
          <div className="journal-controls">
            <input
              className="text-input compact"
              type="text"
              value={journalId}
              onChange={(event) => setJournalId(event.target.value)}
              placeholder="ID, por ejemplo 441A"
              aria-label="ID de particion para journaling"
            />
            <button
              className="button secondary"
              type="button"
              onClick={loadJournal}
              disabled={journalLoading}
            >
              {journalLoading ? 'Cargando...' : 'Ver journaling'}
            </button>
          </div>
        </div>

        {journalError ? <div className="journal-error">{journalError}</div> : null}

        <div className="table-wrap">
          <table className="journal-table">
            <thead>
              <tr>
                <th>#</th>
                <th>Operacion</th>
                <th>Ruta</th>
                <th>Contenido</th>
                <th>Fecha y hora</th>
              </tr>
            </thead>
            <tbody>
              {journalEntries.length === 0 ? (
                <tr>
                  <td colSpan="5" className="empty-cell">
                    No hay transacciones cargadas.
                  </td>
                </tr>
              ) : (
                journalEntries.map((entry) => (
                  <tr key={`${entry.count}-${entry.operation}-${entry.date}`}>
                    <td>{entry.count}</td>
                    <td>{entry.operation}</td>
                    <td>{entry.path}</td>
                    <td>{entry.content}</td>
                    <td>{entry.date}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </section>

      <footer>
        Backend C++ · EXT2/EXT3 simulado en archivos .mia · Frontend React
      </footer>
    </main>
  )
}
