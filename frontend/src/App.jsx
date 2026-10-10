import { useEffect, useMemo, useRef, useState } from 'react'

const API_BASE = (import.meta.env.VITE_API_URL || '').replace(/\/$/, '')

const INITIAL_SCRIPT = `# ExtreamFS
# Escribe comandos aqui o carga un archivo .smia
`

async function apiRequest(path, options = {}) {
  const response = await fetch(`${API_BASE}${path}`, options)
  let data
  try {
    data = await response.json()
  } catch {
    data = { ok: false, error: `Respuesta HTTP ${response.status} no valida` }
  }
  if (!response.ok || !data.ok) throw new Error(data.error || `HTTP ${response.status}`)
  return data
}

function formatBytes(bytes) {
  const value = Number(bytes || 0)
  if (value < 1024) return `${value} B`
  if (value < 1024 * 1024) return `${(value / 1024).toFixed(1)} KiB`
  return `${(value / (1024 * 1024)).toFixed(2)} MiB`
}

function typeLabel(type) {
  if (type === 'extended') return 'Extendida'
  if (type === 'logical') return 'Lógica'
  return 'Primaria'
}

export default function App() {
  const [view, setView] = useState('terminal')
  const [commands, setCommands] = useState(INITIAL_SCRIPT)
  const [output, setOutput] = useState('')
  const [running, setRunning] = useState(false)
  const [status, setStatus] = useState('Listo')
  const [session, setSession] = useState({ active: false })

  const [loginForm, setLoginForm] = useState({ user: '', password: '', id: '' })
  const [loginError, setLoginError] = useState('')
  const [loginLoading, setLoginLoading] = useState(false)

  const [disks, setDisks] = useState([])
  const [selectedDisk, setSelectedDisk] = useState(null)
  const [partitions, setPartitions] = useState([])
  const [selectedPartition, setSelectedPartition] = useState(null)
  const [explorerLoading, setExplorerLoading] = useState(false)
  const [explorerError, setExplorerError] = useState('')
  const [currentPath, setCurrentPath] = useState('/')
  const [entries, setEntries] = useState([])
  const [openedFile, setOpenedFile] = useState(null)

  const [journalId, setJournalId] = useState('')
  const [journalEntries, setJournalEntries] = useState([])
  const [journalError, setJournalError] = useState('')
  const [journalLoading, setJournalLoading] = useState(false)

  const fileInput = useRef(null)

  const breadcrumbs = useMemo(() => {
    const parts = currentPath.split('/').filter(Boolean)
    const crumbs = [{ label: '/', path: '/' }]
    let accumulated = ''
    for (const part of parts) {
      accumulated += `/${part}`
      crumbs.push({ label: part, path: accumulated })
    }
    return crumbs
  }, [currentPath])

  const refreshSession = async () => {
    try {
      const data = await apiRequest('/api/session')
      setSession(data)
      if (data.active) {
        setJournalId((current) => current || data.id || '')
      }
      return data
    } catch (error) {
      setStatus(`No se pudo consultar la sesión: ${error.message}`)
      return { active: false }
    }
  }

  const loadDisks = async () => {
    try {
      const data = await apiRequest('/api/disks')
      setDisks(Array.isArray(data.disks) ? data.disks : [])
      return data.disks || []
    } catch (error) {
      setExplorerError(error.message)
      return []
    }
  }

  useEffect(() => {
    refreshSession()
    loadDisks()
  }, [])

  const loadScript = async (event) => {
    const file = event.target.files?.[0]
    if (!file) return
    if (!file.name.toLowerCase().endsWith('.smia')) {
      setStatus('El script debe tener extensión .smia')
      event.target.value = ''
      return
    }
    try {
      setCommands(await file.text())
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

    if (containsFdiskDelete && !window.confirm(
      'El script contiene FDISK -delete. Esta operación puede eliminar una partición y sus datos. ¿Desea continuar?',
    )) {
      setStatus('Eliminación cancelada por el usuario')
      return
    }

    if (containsLoss && !window.confirm(
      'El script contiene LOSS. Se limpiarán bitmaps, inodos y bloques de la partición EXT3 indicada. ¿Desea continuar?',
    )) {
      setStatus('Simulación LOSS cancelada por el usuario')
      return
    }

    setRunning(true)
    setStatus('Ejecutando...')
    try {
      const data = await apiRequest('/api/execute', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ commands }),
      })
      setOutput(data.output || '')
      setStatus('Ejecución completada')
      await refreshSession()
      await loadDisks()
    } catch (error) {
      setOutput(`[ERROR] FRONTEND: ${error.message}`)
      setStatus('Error al comunicarse con el backend')
    } finally {
      setRunning(false)
    }
  }

  const openLogin = (id = '') => {
    setLoginError('')
    setLoginForm((current) => ({ ...current, id: id || current.id }))
    setView('login')
  }

  const login = async (event) => {
    event.preventDefault()
    if (!loginForm.user.trim() || !loginForm.password || !loginForm.id.trim()) {
      setLoginError('Usuario, contraseña e ID son obligatorios')
      return
    }
    setLoginLoading(true)
    setLoginError('')
    try {
      const data = await apiRequest('/api/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          user: loginForm.user.trim(),
          password: loginForm.password,
          id: loginForm.id.trim(),
        }),
      })
      setSession(data)
      setJournalId(data.id || '')
      setLoginForm((current) => ({ ...current, password: '' }))
      setStatus(`Sesión iniciada como ${data.user}`)
      await loadDisks()
      setView('explorer')
    } catch (error) {
      setLoginError(error.message)
    } finally {
      setLoginLoading(false)
    }
  }

  const logout = async () => {
    try {
      const data = await apiRequest('/api/logout', { method: 'POST' })
      setSession(data)
      setSelectedDisk(null)
      setSelectedPartition(null)
      setPartitions([])
      setEntries([])
      setOpenedFile(null)
      setCurrentPath('/')
      setStatus('Sesión cerrada')
      setView('terminal')
    } catch (error) {
      setStatus(error.message)
    }
  }

  const openExplorer = async () => {
    setExplorerError('')
    setOpenedFile(null)
    await loadDisks()
    setView('explorer')
  }

  const selectDisk = async (disk) => {
    setSelectedDisk(disk)
    setSelectedPartition(null)
    setEntries([])
    setOpenedFile(null)
    setCurrentPath('/')
    setExplorerError('')
    setExplorerLoading(true)
    try {
      const data = await apiRequest('/api/partitions', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: disk.path }),
      })
      setPartitions(Array.isArray(data.partitions) ? data.partitions : [])
    } catch (error) {
      setPartitions([])
      setExplorerError(error.message)
    } finally {
      setExplorerLoading(false)
    }
  }

  const loadDirectory = async (path, partition = selectedPartition) => {
    if (!partition?.id) return
    setExplorerLoading(true)
    setExplorerError('')
    setOpenedFile(null)
    try {
      const data = await apiRequest('/api/fs/list', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: partition.id, path }),
      })
      setEntries(Array.isArray(data.entries) ? data.entries : [])
      setCurrentPath(path)
    } catch (error) {
      setEntries([])
      setExplorerError(error.message)
    } finally {
      setExplorerLoading(false)
    }
  }

  const selectPartition = async (partition) => {
    setSelectedPartition(partition)
    setOpenedFile(null)
    setCurrentPath('/')
    setEntries([])
    setExplorerError('')

    if (!partition.mounted || !partition.id) {
      setExplorerError('La partición debe estar montada para explorar su sistema de archivos.')
      return
    }
    if (!session.active || session.id?.toLowerCase() !== partition.id.toLowerCase()) {
      setLoginForm((current) => ({ ...current, id: partition.id }))
      setExplorerError(`Inicia sesión sobre ${partition.id} para explorar esta partición.`)
      return
    }
    await loadDirectory('/', partition)
  }

  const openFile = async (entry) => {
    if (!selectedPartition?.id || !entry.readable) return
    setExplorerLoading(true)
    setExplorerError('')
    try {
      const data = await apiRequest('/api/fs/file', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id: selectedPartition.id, path: entry.path }),
      })
      setOpenedFile(data.file || null)
    } catch (error) {
      setOpenedFile(null)
      setExplorerError(error.message)
    } finally {
      setExplorerLoading(false)
    }
  }

  const loadJournal = async () => {
    const id = journalId.trim()
    if (!id) {
      setJournalError('Ingrese el ID de una partición EXT3 montada')
      setJournalEntries([])
      return
    }
    setJournalLoading(true)
    setJournalError('')
    try {
      const data = await apiRequest('/api/journaling', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ id }),
      })
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
          <p>C++ DISK ONLINE · EXT2/EXT3 · terminal y explorador visual.</p>
        </div>
        <div className="hero-side">
          <div className="status-pill" aria-live="polite">
            <span className={running || explorerLoading ? 'dot busy' : 'dot'} />
            {status}
          </div>
          {session.active ? (
            <div className="session-pill">
              <strong>{session.user}</strong>
              <span>{session.id}</span>
            </div>
          ) : null}
        </div>
      </header>

      <nav className="top-nav" aria-label="Navegación principal">
        <button className={view === 'terminal' ? 'nav-button active' : 'nav-button'} onClick={() => setView('terminal')}>Terminal</button>
        <button className={view === 'explorer' ? 'nav-button active' : 'nav-button'} onClick={openExplorer}>Explorador</button>
        <button className={view === 'journal' ? 'nav-button active' : 'nav-button'} onClick={() => setView('journal')}>Journaling</button>
        <span className="nav-spacer" />
        {session.active ? (
          <button className="nav-button danger" onClick={logout}>Cerrar sesión</button>
        ) : (
          <button className={view === 'login' ? 'nav-button active' : 'nav-button'} onClick={() => openLogin()}>Iniciar sesión</button>
        )}
      </nav>

      {view === 'terminal' ? (
        <>
          <section className="panel">
            <div className="panel-heading">
              <div>
                <h2>Entrada de comandos</h2>
                <p>Crea discos, particiones, monta y administra el sistema desde el backend C++.</p>
              </div>
              <div className="actions">
                <input ref={fileInput} className="visually-hidden" type="file" accept=".smia,text/plain" onChange={loadScript} />
                <button className="button secondary" type="button" onClick={() => fileInput.current?.click()} disabled={running}>Cargar .smia</button>
                <button className="button primary" type="button" onClick={execute} disabled={running}>{running ? 'Ejecutando...' : 'Ejecutar'}</button>
                <button className="button ghost" type="button" onClick={clearAll} disabled={running}>Limpiar</button>
              </div>
            </div>
            <textarea className="terminal input" aria-label="Entrada de comandos" spellCheck="false" value={commands} onChange={(event) => setCommands(event.target.value)} placeholder="mkdisk -size=10 -unit=M -path=/tmp/disco.mia" />
          </section>

          <section className="panel">
            <div className="panel-heading">
              <div>
                <h2>Salida del servidor</h2>
                <p>Respuesta de la API REST implementada directamente en C++.</p>
              </div>
            </div>
            <textarea className="terminal output" aria-label="Salida de comandos" readOnly value={output} placeholder="La salida de la ejecución aparecerá aquí..." />
          </section>
        </>
      ) : null}

      {view === 'login' ? (
        <section className="panel login-panel">
          <div className="panel-heading">
            <div>
              <span className="section-kicker">Autenticación</span>
              <h2>Iniciar sesión</h2>
              <p>Usa un usuario de la partición montada. La sesión se mantiene para los comandos y el explorador.</p>
            </div>
          </div>
          <form className="login-form" onSubmit={login}>
            <label>
              <span>Usuario</span>
              <input className="text-input" value={loginForm.user} onChange={(e) => setLoginForm({ ...loginForm, user: e.target.value })} placeholder="root" autoComplete="username" />
            </label>
            <label>
              <span>Contraseña</span>
              <input className="text-input" type="password" value={loginForm.password} onChange={(e) => setLoginForm({ ...loginForm, password: e.target.value })} placeholder="••••••••" autoComplete="current-password" />
            </label>
            <label>
              <span>ID de partición</span>
              <input className="text-input" value={loginForm.id} onChange={(e) => setLoginForm({ ...loginForm, id: e.target.value })} placeholder="441A" />
            </label>
            <button className="button primary login-submit" disabled={loginLoading}>{loginLoading ? 'Validando...' : 'Ingresar'}</button>
          </form>
          {loginError ? <div className="message error">{loginError}</div> : null}
        </section>
      ) : null}

      {view === 'explorer' ? (
        <section className="panel explorer-panel">
          <div className="panel-heading">
            <div>
              <span className="section-kicker">Solo lectura</span>
              <h2>Visualizador del sistema de archivos</h2>
              <p>Selecciona disco, partición y navega desde la raíz /. Las modificaciones se realizan desde la terminal.</p>
            </div>
            <button className="button ghost" onClick={loadDisks}>Actualizar</button>
          </div>

          {!session.active ? (
            <div className="message info">
              Debes iniciar sesión para abrir el contenido del sistema de archivos. Puedes inspeccionar discos y particiones antes de ingresar.
              <button className="inline-action" onClick={() => openLogin()}>Iniciar sesión</button>
            </div>
          ) : null}
          {explorerError ? <div className="message error">{explorerError}</div> : null}

          <div className="explorer-grid">
            <aside className="explorer-sidebar">
              <div className="sidebar-title">Discos</div>
              {disks.length === 0 ? <div className="empty-state">No hay discos conocidos. Créalo y móntalo desde Terminal.</div> : null}
              {disks.map((disk) => (
                <button key={disk.path} className={selectedDisk?.path === disk.path ? 'disk-card selected' : 'disk-card'} onClick={() => selectDisk(disk)}>
                  <span className="disk-icon">◉</span>
                  <span className="disk-main">
                    <strong>{disk.name}</strong>
                    <small>{formatBytes(disk.size)} · {disk.fit}</small>
                  </span>
                  <span className="badge">{disk.mountedCount} mont.</span>
                </button>
              ))}
            </aside>

            <div className="explorer-content">
              {!selectedDisk ? (
                <div className="empty-stage">
                  <div className="empty-icon">▣</div>
                  <h3>Selecciona un disco</h3>
                  <p>Se mostrarán su capacidad, fit, particiones y estado de montaje.</p>
                </div>
              ) : (
                <>
                  <div className="disk-summary">
                    <div><span>Disco</span><strong>{selectedDisk.name}</strong></div>
                    <div><span>Capacidad</span><strong>{formatBytes(selectedDisk.size)}</strong></div>
                    <div><span>Fit</span><strong>{selectedDisk.fit}</strong></div>
                    <div><span>Creado</span><strong>{selectedDisk.created || '—'}</strong></div>
                  </div>

                  <div className="subheading">Particiones</div>
                  <div className="partition-grid">
                    {partitions.map((partition) => (
                      <button key={`${partition.type}-${partition.start}-${partition.name}`} className={selectedPartition?.start === partition.start ? 'partition-card selected' : 'partition-card'} onClick={() => selectPartition(partition)}>
                        <div className="partition-top"><strong>{partition.name}</strong><span className={partition.mounted ? 'state mounted' : 'state'}>{partition.mounted ? 'Montada' : 'Desmontada'}</span></div>
                        <div className="partition-meta"><span>{typeLabel(partition.type)}</span><span>{formatBytes(partition.size)}</span><span>Fit {partition.fit}</span></div>
                        <div className="partition-id">{partition.id || 'Sin ID'}</div>
                      </button>
                    ))}
                    {partitions.length === 0 && !explorerLoading ? <div className="empty-state wide">Este disco no tiene particiones.</div> : null}
                  </div>

                  {selectedPartition ? (
                    <div className="filesystem-stage">
                      <div className="filesystem-toolbar">
                        <div className="breadcrumbs" aria-label="Ruta actual">
                          {breadcrumbs.map((crumb, index) => (
                            <span key={crumb.path}>
                              {index > 0 ? <span className="crumb-separator">/</span> : null}
                              <button onClick={() => loadDirectory(crumb.path)} disabled={!selectedPartition.id || !session.active}>{crumb.label}</button>
                            </span>
                          ))}
                        </div>
                        <div className="partition-session">{selectedPartition.id || 'No montada'}</div>
                      </div>

                      <div className="file-table-wrap">
                        <table className="file-table">
                          <thead><tr><th>Nombre</th><th>Tipo</th><th>Tamaño</th><th>Permisos</th><th>UID/GID</th><th>Modificado</th></tr></thead>
                          <tbody>
                            {entries.map((entry) => (
                              <tr key={`${entry.inode}-${entry.path}`} className={!entry.readable ? 'locked-row' : ''}>
                                <td>
                                  <button className="file-link" disabled={!entry.readable} onClick={() => entry.type === 'folder' ? loadDirectory(entry.path) : openFile(entry)}>
                                    <span className="file-icon">{entry.type === 'folder' ? '▰' : '▤'}</span>{entry.name}{!entry.readable ? ' 🔒' : ''}
                                  </button>
                                </td>
                                <td>{entry.type === 'folder' ? 'Carpeta' : 'Archivo'}</td>
                                <td>{formatBytes(entry.size)}</td>
                                <td><code>{entry.permissions}</code></td>
                                <td>{entry.uid}/{entry.gid}</td>
                                <td>{entry.modified || '—'}</td>
                              </tr>
                            ))}
                            {entries.length === 0 ? <tr><td colSpan="6" className="empty-cell">{explorerLoading ? 'Cargando...' : 'No hay elementos para mostrar.'}</td></tr> : null}
                          </tbody>
                        </table>
                      </div>

                      {openedFile ? (
                        <div className="file-preview">
                          <div className="file-preview-head">
                            <div><span>Archivo de texto</span><h3>{openedFile.name}</h3></div>
                            <button className="button ghost" onClick={() => setOpenedFile(null)}>Cerrar</button>
                          </div>
                          <div className="file-facts"><span>{formatBytes(openedFile.size)}</span><span>Permisos {openedFile.permissions}</span><span>UID/GID {openedFile.uid}/{openedFile.gid}</span></div>
                          <pre>{openedFile.content}</pre>
                        </div>
                      ) : null}
                    </div>
                  ) : null}
                </>
              )}
            </div>
          </div>
        </section>
      ) : null}

      {view === 'journal' ? (
        <section className="panel journal-panel">
          <div className="panel-heading">
            <div>
              <span className="section-kicker">EXT3</span>
              <h2>Journaling EXT3</h2>
              <p>Transacciones persistidas con operación, ruta, contenido, fecha y hora.</p>
            </div>
            <div className="journal-controls">
              <input className="text-input compact" type="text" value={journalId} onChange={(event) => setJournalId(event.target.value)} placeholder="ID, por ejemplo 441A" aria-label="ID de partición para journaling" />
              <button className="button secondary" type="button" onClick={loadJournal} disabled={journalLoading}>{journalLoading ? 'Cargando...' : 'Ver journaling'}</button>
            </div>
          </div>
          {journalError ? <div className="message error">{journalError}</div> : null}
          <div className="table-wrap">
            <table className="journal-table">
              <thead><tr><th>#</th><th>Operacion</th><th>Ruta</th><th>Contenido</th><th>Fecha y hora</th></tr></thead>
              <tbody>
                {journalEntries.length === 0 ? <tr><td colSpan="5" className="empty-cell">No hay transacciones cargadas.</td></tr> : journalEntries.map((entry) => (
                  <tr key={`${entry.count}-${entry.operation}-${entry.date}`}><td>{entry.count}</td><td>{entry.operation}</td><td>{entry.path}</td><td>{entry.content}</td><td>{entry.date}</td></tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      ) : null}

      <footer>Backend C++ · React · EXT2/EXT3 · visualizador de solo lectura</footer>
    </main>
  )
}
