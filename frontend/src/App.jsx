import { useRef, useState } from 'react'

const INITIAL_SCRIPT = `# ExtreamFS
# Escribe comandos aqui o carga un archivo .smia
`

export default function App() {
  const [commands, setCommands] = useState(INITIAL_SCRIPT)
  const [output, setOutput] = useState('')
  const [running, setRunning] = useState(false)
  const [status, setStatus] = useState('Listo')
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

    const containsFdiskDelete = commands
      .split('\n')
      .some((line) => {
        const clean = line.trim()
        return clean && !clean.startsWith('#') && /\bfdisk\b/i.test(clean) && /-delete\s*=/i.test(clean)
      })

    if (containsFdiskDelete) {
      const confirmed = window.confirm(
        'El script contiene FDISK -delete. Esta operacion puede eliminar una particion y sus datos. ¿Desea continuar?',
      )
      if (!confirmed) {
        setStatus('Eliminacion cancelada por el usuario')
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
          <p>Interfaz web para ejecutar comandos sobre el sistema de archivos EXT2.</p>
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

      <footer>
        Backend C++ · EXT2 simulado en archivos .mia · Frontend React
      </footer>
    </main>
  )
}
