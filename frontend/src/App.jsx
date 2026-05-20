import React, { useState, useEffect, useRef, useCallback } from 'react';
import './index.css';

const API = 'http://localhost:8000';

const RefreshIcon = () => (
  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
    <polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
  </svg>
);

const ArrowsIcon = () => (
  <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round">
    <polyline points="15 18 9 12 15 6"/>
    <polyline points="9 18 3 12 9 6" transform="translate(12 0)"/>
  </svg>
);

const CameraIcon = () => (
  <svg className="empty-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round">
    <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/>
    <circle cx="12" cy="13" r="4"/>
  </svg>
);

const DEFAULT_PARAMS = {
  stock: 'none',
  scanner: 'none',
  exposure: 0.0,
  highlights: 0,
  shadows: 0,
  blacks: 0,
  whites: 0,
  midtones: 0,
  contrast: 0,
  temp: 0,
  tint: 0,
  adaptation: 1.0,
  grain: 'Auto',
  halation: 'Auto',
};

export default function App() {
  const [files, setFiles] = useState([]);
  const [profiles, setProfiles] = useState({ stocks: [], scanners: [] });
  const [selectedFile, setSelectedFile] = useState(null);
  const [loadingFiles, setLoadingFiles] = useState(false);
  const [selectLoading, setSelectLoading] = useState(false);
  const [diagnostics, setDiagnostics] = useState(null);
  const [params, setParams] = useState(DEFAULT_PARAMS);
  const [previewUrl, setPreviewUrl] = useState('');
  const [rawUrl, setRawUrl] = useState('');
  const [previewReady, setPreviewReady] = useState(false);
  const [previewLoading, setPreviewLoading] = useState(false);
  const [exporting, setExporting] = useState(false);
  const [toast, setToast] = useState(null);
  const [comparePos, setComparePos] = useState(50);
  const [dragging, setDragging] = useState(false);
  const [diagOpen, setDiagOpen] = useState(true);
  const containerRef = useRef(null);
  const debounceRef = useRef(null);
  const inflightRef = useRef(null); // cancel stale image loads

  const showToast = useCallback((message, type = 'success') => {
    setToast({ message, type });
    setTimeout(() => setToast(null), 4000);
  }, []);

  const set = (key) => (e) => {
    const val = e.target.type === 'range'
      ? (key === 'exposure' || key === 'adaptation' ? parseFloat(e.target.value) : parseInt(e.target.value))
      : e.target.value;
    setParams(p => ({ ...p, [key]: val }));
  };

  // Boot
  useEffect(() => {
    fetchFiles();
    fetchProfiles();
  }, []);

  const fetchFiles = async () => {
    setLoadingFiles(true);
    try {
      const res = await fetch(`${API}/api/files`);
      const data = await res.json();
      setFiles(data);
    } catch {
      showToast('Cannot reach backend — is server.py running?', 'error');
    } finally {
      setLoadingFiles(false);
    }
  };

  const fetchProfiles = async () => {
    try {
      const res = await fetch(`${API}/api/profiles`);
      const data = await res.json();
      setProfiles(data);
    } catch {}
  };

  const selectFile = async (filename) => {
    setSelectLoading(true);
    setPreviewReady(false);
    try {
      const res = await fetch(`${API}/api/select`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ filename }),
      });
      if (!res.ok) throw new Error(await res.text());
      const data = await res.json();
      setDiagnostics(data.diagnostics);
      setRawUrl(`${API}/api/raw-image?t=${Date.now()}`);
      // Set selectedFile AFTER server confirms session is ready
      // This prevents the preview useEffect from firing before the session exists
      setSelectedFile(filename);
    } catch (e) {
      showToast(`Load failed: ${e.message}`, 'error');
    } finally {
      setSelectLoading(false);
    }
  };

  // Debounced preview — URL built INSIDE effect so params are never stale
  useEffect(() => {
    if (!selectedFile) return;

    // When no stock is selected, show the raw image directly
    if (params.stock === 'none') {
      setPreviewUrl(rawUrl);
      setPreviewReady(!!rawUrl);
      return;
    }

    if (debounceRef.current) clearTimeout(debounceRef.current);

    debounceRef.current = setTimeout(() => {
      // Build URL here, directly from current params in scope
      const url = `${API}/api/preview?filename=${encodeURIComponent(selectedFile)}`
        + `&stock=${encodeURIComponent(params.stock)}`
        + `&scanner=${encodeURIComponent(params.scanner)}`
        + `&exposure=${params.exposure}`
        + `&highlights=${params.highlights}`
        + `&shadows=${params.shadows}`
        + `&blacks=${params.blacks}`
        + `&whites=${params.whites}`
        + `&midtones=${params.midtones}`
        + `&contrast=${params.contrast}`
        + `&temp=${params.temp}`
        + `&tint=${params.tint}`
        + `&adaptation=${params.adaptation}`
        + `&grain=${encodeURIComponent(params.grain)}`
        + `&halation=${encodeURIComponent(params.halation)}`
        + `&t=${Date.now()}`;

      setPreviewLoading(true);

      // Cancel any previous in-flight image load
      if (inflightRef.current) { inflightRef.current.onload = null; inflightRef.current.onerror = null; }

      const img = new Image();
      inflightRef.current = img;
      img.onload = () => {
        setPreviewUrl(url);
        setPreviewReady(true);
        setPreviewLoading(false);
      };
      img.onerror = () => {
        setPreviewLoading(false);
        showToast('Preview render failed — check server terminal for details', 'error');
      };
      img.src = url;
    }, 200);

    return () => clearTimeout(debounceRef.current);
  }, [
    selectedFile,
    params.stock, params.scanner, params.exposure, params.highlights,
    params.shadows, params.blacks, params.whites, params.midtones,
    params.contrast, params.temp, params.tint,
    params.adaptation, params.grain, params.halation,
  ]);

  const handleExport = async () => {
    if (!selectedFile) return;
    setExporting(true);
    try {
      const res = await fetch(`${API}/api/export`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          filename: selectedFile,
          stock: params.stock,
          scanner: params.scanner,
          exposure: params.exposure,
          highlights: params.highlights,
          shadows: params.shadows,
          blacks: params.blacks,
          whites: params.whites,
          midtones: params.midtones,
          contrast: params.contrast,
          temp: params.temp,
          tint: params.tint,
          adaptation: params.adaptation,
          grain: params.grain,
          halation: params.halation,
        }),
      });
      if (!res.ok) throw new Error(await res.text());
      const data = await res.json();
      showToast(`Saved to: ${data.output_path}`, 'success');
    } catch (e) {
      showToast(`Export failed: ${e.message}`, 'error');
    } finally {
      setExporting(false);
    }
  };

  const applyPreset = (name) => {
    const presets = {
      k64:   { stock: 'kodachrome_64', scanner: 'frontier_soft',   exposure: 0.15, contrast: 20,  temp: 15, tint: 5,   adaptation: 1.0 },
      portra:{ stock: 'portra_400',    scanner: 'noritsu_smooth',  exposure: 0.4,  contrast: -10, temp: 5,  tint: -10, adaptation: 0.85 },
      trix:  { stock: 'tri_x_400',     scanner: 'darkroom_print',  exposure: 0.0,  contrast: 30,  temp: 0,  tint: 0,   adaptation: 1.0 },
    };
    setParams(p => ({ ...p, ...presets[name] }));
  };

  // Compare drag
  useEffect(() => {
    const onMove = (e) => {
      if (!dragging || !containerRef.current) return;
      const rect = containerRef.current.getBoundingClientRect();
      const pct = Math.max(0, Math.min(100, ((e.clientX - rect.left) / rect.width) * 100));
      setComparePos(pct);
    };
    const onUp = () => setDragging(false);
    if (dragging) {
      window.addEventListener('mousemove', onMove);
      window.addEventListener('mouseup', onUp);
    }
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, [dragging]);

  const fmtVal = (key, v) => {
    if (key === 'exposure') return (v > 0 ? '+' : '') + v.toFixed(2) + ' EV';
    if (key === 'adaptation') return v.toFixed(2);
    return (v > 0 ? '+' : '') + v;
  };

  const sliderGroups = [
    {
      title: 'Light',
      rows: [
        { key: 'exposure',   label: 'Exposure',   min: -3,    max: 3,    step: 0.05 },
        { key: 'contrast',   label: 'Contrast',   min: -100,  max: 100,  step: 1 },
        { key: 'highlights', label: 'Highlights', min: -100,  max: 100,  step: 1 },
        { key: 'shadows',    label: 'Shadows',    min: -100,  max: 100,  step: 1 },
        { key: 'whites',     label: 'Whites',     min: -100,  max: 100,  step: 1 },
        { key: 'blacks',     label: 'Blacks',     min: -100,  max: 100,  step: 1 },
        { key: 'midtones',   label: 'Midtones',   min: -100,  max: 100,  step: 1 },
      ],
    },
    {
      title: 'Color',
      rows: [
        { key: 'temp', label: 'Temperature', min: -100, max: 100, step: 1 },
        { key: 'tint', label: 'Tint',        min: -100, max: 100, step: 1 },
      ],
    },
  ];

  return (
    <div className="dfee-app">

      {/* Header */}
      <header className="dfee-header">
        <div className="header-brand">
          <span className="brand-name">DFEE</span>
          <span className="brand-tag">Film Workspace</span>
        </div>
        <span className="header-status">
          {selectedFile ? selectedFile : 'No image loaded'}
        </span>
      </header>

      {/* Workspace */}
      <div className="dfee-workspace">

        {/* Sidebar */}
        <aside className="dfee-sidebar">
          <div className="sidebar-header">
            <span className="sidebar-label">Library</span>
            <button className="icon-btn" onClick={fetchFiles} title="Refresh">
              <RefreshIcon />
            </button>
          </div>

          <div className="file-list">
            {loadingFiles ? (
              <div className="empty-msg">Scanning…</div>
            ) : files.length === 0 ? (
              <div className="empty-msg">
                No .ARW files found.<br />Place images in <code style={{ color: 'var(--text-secondary)' }}>raw_files/</code>
              </div>
            ) : (
              files.map(f => (
                <div
                  key={f.filename}
                  className={`file-item ${selectedFile === f.filename ? 'active' : ''}`}
                  onClick={() => selectFile(f.filename)}
                >
                  <span className="file-name">{f.filename}</span>
                  <span className="file-size">{f.size_mb} MB</span>
                </div>
              ))
            )}
          </div>

          {selectedFile && (
            <div className="sidebar-presets">
              <span className="presets-label">Presets</span>
              <div className="preset-row">
                <button className="preset-btn" onClick={() => applyPreset('k64')}>K64</button>
                <button className="preset-btn" onClick={() => applyPreset('portra')}>Portra</button>
                <button className="preset-btn" onClick={() => applyPreset('trix')}>Tri-X</button>
              </div>
            </div>
          )}
        </aside>

        {/* Viewer */}
        <main className="dfee-viewer">
          {selectLoading ? (
            <div className="loading-card">
              <div className="spinner" />
              <p>Loading RAW…</p>
              <span>Demosaicing & building zone masks</span>
            </div>
          ) : selectedFile && previewReady ? (
            <div className="viewer-inner">
              {/* Subtle loading pulse when re-rendering */}
              {previewLoading && (
                <div style={{
                  position: 'absolute', top: 12, right: 16, zIndex: 20,
                  display: 'flex', alignItems: 'center', gap: 7,
                  background: 'rgba(0,0,0,0.55)', borderRadius: 4,
                  padding: '4px 10px', backdropFilter: 'blur(4px)',
                }}>
                  <div className="spinner" style={{ width: 12, height: 12, borderWidth: 2 }} />
                  <span style={{ fontSize: 11, color: 'rgba(255,255,255,0.6)' }}>Rendering…</span>
                </div>
              )}
              <div className="view-labels">
                <span className="view-label">RAW</span>
                <span className="view-label">DFEE</span>
              </div>
              <div className="compare-wrap" ref={containerRef}>
                {/* After (film) */}
                <div className="img-after">
                  <img src={previewUrl} alt="Film emulation" />
                </div>
                {/* Before (raw) clipped */}
                <div
                  className="img-before-wrap"
                  style={{ clipPath: `polygon(0 0, ${comparePos}% 0, ${comparePos}% 100%, 0 100%)` }}
                >
                  <img src={rawUrl} alt="RAW sensor" />
                </div>
                {/* Divider */}
                <div
                  className="compare-divider"
                  style={{ left: `${comparePos}%` }}
                  onMouseDown={() => setDragging(true)}
                >
                  <div className="compare-handle">
                    <ArrowsIcon />
                  </div>
                </div>
              </div>
            </div>
          ) : (
            <div className="empty-state">
              <CameraIcon />
              <h3>No image selected</h3>
              <p>Choose a RAW file from the library to begin.</p>
            </div>
          )}
        </main>

        {/* Controls */}
        <aside className="dfee-controls">
          <div className="controls-body">

            {/* Profile selectors */}
            <div className="control-group">
              <div className="group-title">Profile</div>
              <div className="field">
                <label className="field-label">Film Stock</label>
                <select className="select" value={params.stock} onChange={set('stock')}>
                  {profiles.stocks.map(p => (
                    <option key={p.id} value={p.id}>{p.name}</option>
                  ))}
                </select>
              </div>
              <div className="field">
                <label className="field-label">Scanner / Finish</label>
                <select className="select" value={params.scanner} onChange={set('scanner')}>
                  {profiles.scanners.map(p => (
                    <option key={p.id} value={p.id}>{p.name}</option>
                  ))}
                </select>
              </div>
            </div>

            {/* Light + Color sliders */}
            {sliderGroups.map(group => (
              <div className="control-group" key={group.title}>
                <div className="group-title">{group.title}</div>
                {group.rows.map(({ key, label, min, max, step }) => {
                  const isDirty = params[key] !== DEFAULT_PARAMS[key];
                  return (
                    <div className="slider-row" key={key}>
                      <div className="slider-header">
                        <span className="slider-label">{label}</span>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          {isDirty && (
                            <button
                              className="revert-btn"
                              title={`Reset ${label} to default`}
                              onClick={() => setParams(p => ({ ...p, [key]: DEFAULT_PARAMS[key] }))}
                            >↺</button>
                          )}
                          <span className={`slider-value${isDirty ? ' slider-value--dirty' : ''}`}>
                            {fmtVal(key, params[key])}
                          </span>
                        </div>
                      </div>
                      <input
                        type="range" min={min} max={max} step={step}
                        value={params[key]} onChange={set(key)}
                        className={`slider${isDirty ? ' slider--dirty' : ''}`}
                      />
                    </div>
                  );
                })}
              </div>
            ))}

            {/* Film modifiers */}
            <div className="control-group">
              <div className="group-title">Film Modifiers</div>
              <div className="slider-row">
                <div className="slider-header">
                  <span className="slider-label">Adaptation</span>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                    {params.adaptation !== DEFAULT_PARAMS.adaptation && (
                      <button
                        className="revert-btn"
                        title="Reset Adaptation to default"
                        onClick={() => setParams(p => ({ ...p, adaptation: DEFAULT_PARAMS.adaptation }))}
                      >↺</button>
                    )}
                    <span className={`slider-value${params.adaptation !== DEFAULT_PARAMS.adaptation ? ' slider-value--dirty' : ''}`}>
                      {params.adaptation.toFixed(2)}
                    </span>
                  </div>
                </div>
                <input type="range" min={0} max={1} step={0.05}
                  value={params.adaptation} onChange={set('adaptation')}
                  className={`slider${params.adaptation !== DEFAULT_PARAMS.adaptation ? ' slider--dirty' : ''}`} />
              </div>
              <div className="field" style={{ marginTop: 12 }}>
                <label className="field-label">Grain</label>
                <select className="select" value={params.grain} onChange={set('grain')}>
                  {['Auto','Off','Low','Medium','High'].map(v => <option key={v}>{v}</option>)}
                </select>
              </div>
              <div className="field">
                <label className="field-label">Halation</label>
                <select className="select" value={params.halation} onChange={set('halation')}>
                  {['Auto','Off','Low','Medium','High'].map(v => <option key={v}>{v}</option>)}
                </select>
              </div>
            </div>

            {/* Diagnostics */}
            {diagnostics && (
              <div className="control-group">
                <div
                  className={`group-title group-toggle ${diagOpen ? 'open' : ''}`}
                  onClick={() => setDiagOpen(v => !v)}
                >
                  <span>Diagnostics</span>
                  <span className={`toggle-chevron ${diagOpen ? 'open' : ''}`}>›</span>
                </div>
                {diagOpen && (
                  <>
                    <div className="diag-grid">
                      {[
                        ['Tonal key',    diagnostics.tonal_skew],
                        ['Dyn. range',   `${diagnostics.dynamic_range_stops} EV`],
                        ['Midtone',      diagnostics.midtone_anchor],
                        ['Highlights',   diagnostics.highlight_headroom],
                        ['Shadows',      diagnostics.shadow_depth],
                        ['Neon risk',    diagnostics.neon_risk],
                        ['Neutral',      diagnostics.neutral_confidence],
                        ['Palette',      diagnostics.palette_entropy],
                      ].map(([k, v]) => (
                        <div className="diag-row" key={k}>
                          <span className="diag-key">{k}</span>
                          <span className="diag-val">{v}</span>
                        </div>
                      ))}
                    </div>

                    <div className="warnings-list">
                      {diagnostics.dynamic_range_stops > 11.5 && (
                        <div className="warning-row">
                          <div className="warning-dot" />
                          <span className="warning-text">Harsh contrast — shoulder softened</span>
                        </div>
                      )}
                      {diagnostics.shadow_depth < 0.02 && (
                        <div className="warning-row">
                          <div className="warning-dot" />
                          <span className="warning-text">Shadow noise risk — grain reduced</span>
                        </div>
                      )}
                      {diagnostics.neon_risk > 0.03 && (
                        <div className="warning-row">
                          <div className="warning-dot" />
                          <span className="warning-text">Neon chroma — gamut compression applied</span>
                        </div>
                      )}
                    </div>
                  </>
                )}
              </div>
            )}
          </div>

          {/* Export */}
          {selectedFile && (
            <div className="controls-footer">
              <button className="export-btn" onClick={handleExport} disabled={exporting || !previewReady}>
                {exporting ? <><div className="spinner" style={{ width: 14, height: 14, borderWidth: 2 }} /> Exporting…</> : 'Export 16-bit TIFF'}
              </button>
            </div>
          )}
        </aside>
      </div>

      {/* Export overlay */}
      {exporting && (
        <div className="export-overlay">
          <div className="export-card">
            <div className="spinner" style={{ width: 32, height: 32 }} />
            <p>Rendering full resolution…</p>
            <span>Writing 16-bit TIFF and sidecar JSON</span>
          </div>
        </div>
      )}

      {/* Toast */}
      {toast && (
        <div className={`toast ${toast.type}`}>
          {toast.type === 'success' ? '✓' : '✕'} {toast.message}
        </div>
      )}
    </div>
  );
}
