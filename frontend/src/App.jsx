import React, { useState, useEffect, useRef, useCallback } from 'react';
import './index.css';
import CurvesPanel, { DEFAULT_POINTS as DEFAULT_CURVES } from './CurvesPanel';
import HslPanel from './HslPanel';

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
  saturation: 0,
  vibrance: 0,
  clarity: 0,
  texture: 0,
  dehaze: 0,
  bloom: 0,
  adaptation: 1.0,
  grain: 'Auto',
  grain_strength: -1.0,
  grain_size: -1.0,
  grain_roughness: -1.0,
  halation: 'Auto',
};

const HSL_RANGES_KEYS = ['red','orange','yellow','green','aqua','blue','purple','magenta'];
const DEFAULT_HSL = Object.fromEntries(
  HSL_RANGES_KEYS.flatMap(r => ['h','s','l'].map(p => [`${r}_${p}`, 0]))
);

export default function App() {
  const [files, setFiles] = useState([]);
  const [profiles, setProfiles] = useState({ stocks: [], scanners: [] });
  const [selectedFile, setSelectedFile] = useState(null);
  const [loadingFiles, setLoadingFiles] = useState(false);
  const [selectLoading, setSelectLoading] = useState(false);
  const [diagnostics, setDiagnostics] = useState(null);
  const [params, setParams] = useState(DEFAULT_PARAMS);
  const [curves, setCurves] = useState(DEFAULT_CURVES);
  const [hsl, setHsl] = useState(DEFAULT_HSL);

  // ── Collapsible sections — persisted to localStorage ───────────────────
  const DEFAULT_OPEN = {
    Profile: true, Curves: true, HSL: false,
    Light: true, Color: true, Detail: false, Effects: false,
    'Film Modifiers': false, Diagnostics: false, History: true,
  };
  const [openSections, setOpenSections] = useState(() => {
    try {
      const saved = localStorage.getItem('dfee_open_sections');
      return saved ? { ...DEFAULT_OPEN, ...JSON.parse(saved) } : DEFAULT_OPEN;
    } catch { return DEFAULT_OPEN; }
  });
  const toggleSection = (name) => setOpenSections(prev => {
    const next = { ...prev, [name]: !prev[name] };
    try { localStorage.setItem('dfee_open_sections', JSON.stringify(next)); } catch {}
    return next;
  });

  // ── Reset All ───────────────────────────────────────────────────────
  const handleResetAll = () => {
    setParams(DEFAULT_PARAMS);
    setCurves(DEFAULT_CURVES);
    setHsl(DEFAULT_HSL);
  };
  const [previewUrl, setPreviewUrl] = useState('');
  const [rawUrl, setRawUrl] = useState('');
  const [previewReady, setPreviewReady] = useState(false);
  const [previewLoading, setPreviewLoading] = useState(false);
  const [exporting, setExporting] = useState(false);
  const [exportFormat, setExportFormat] = useState('png8'); // 'png8' | 'png16' | 'tiff'
  const [toast, setToast] = useState(null);
  const [comparePos, setComparePos] = useState(50);
  const [dragging, setDragging] = useState(false);
  const [diagOpen, setDiagOpen] = useState(true);
  const containerRef   = useRef(null);
  const viewerRef      = useRef(null);  // the .compare-wrap / zoom container
  const debounceRef    = useRef(null);
  const inflightRef    = useRef(null);
  const historyDebRef  = useRef(null);
  const lastPushedRef  = useRef(null);  // snapshot of last history entry pushed

  // ── Zoom state ──────────────────────────────────────────────────────────
  const [zoom,    setZoom]    = useState(1.0); // 1.0 = fit
  const [pan,     setPan]     = useState({ x: 0, y: 0 });
  const [isPanning, setIsPanning] = useState(false);
  const panStart  = useRef(null);
  const FIT = 1.0;

  const clampPan = (px, py, z, rect) => {
    if (!rect) return { x: px, y: py };
    const maxX = Math.max(0, (rect.width  * z - rect.width)  / 2);
    const maxY = Math.max(0, (rect.height * z - rect.height) / 2);
    return { x: Math.max(-maxX, Math.min(maxX, px)), y: Math.max(-maxY, Math.min(maxY, py)) };
  };

  const zoomTo = useCallback((newZ, focalX, focalY) => {
    const el = viewerRef.current;
    if (!el) { setZoom(newZ); setPan({ x: 0, y: 0 }); return; }
    const rect = el.getBoundingClientRect();
    // Adjust pan so focal point stays under cursor
    if (focalX !== undefined) {
      setPan(prev => {
        const ratio = newZ / zoom;
        const fx = focalX - rect.left - rect.width  / 2;
        const fy = focalY - rect.top  - rect.height / 2;
        const nx = (prev.x + fx) * ratio - fx;
        const ny = (prev.y + fy) * ratio - fy;
        return clampPan(nx, ny, newZ, rect);
      });
    } else {
      setPan(prev => clampPan(prev.x, prev.y, newZ, rect));
    }
    setZoom(newZ);
  }, [zoom]);

  const resetZoom = useCallback(() => { setZoom(FIT); setPan({ x: 0, y: 0 }); }, []);

  // Mouse wheel zoom
  useEffect(() => {
    const el = viewerRef.current;
    if (!el) return;
    const onWheel = (e) => {
      if (!previewReady) return;
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.12 : 1 / 1.12;
      const newZ = Math.max(FIT, Math.min(8, zoom * factor));
      zoomTo(newZ, e.clientX, e.clientY);
    };
    el.addEventListener('wheel', onWheel, { passive: false });
    return () => el.removeEventListener('wheel', onWheel);
  }, [zoom, zoomTo, previewReady]);

  // Pan drag
  useEffect(() => {
    if (!isPanning) return;
    const onMove = (e) => {
      if (!panStart.current) return;
      const dx = e.clientX - panStart.current.x;
      const dy = e.clientY - panStart.current.y;
      panStart.current = { x: e.clientX, y: e.clientY };
      const el = viewerRef.current;
      setPan(prev => clampPan(prev.x + dx, prev.y + dy, zoom, el?.getBoundingClientRect()));
    };
    const onUp = () => { setIsPanning(false); panStart.current = null; };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup',   onUp);
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, [isPanning, zoom]);

  // Double-click toggles fit ↔ 2×
  const handleViewerDblClick = useCallback((e) => {
    if (!previewReady) return;
    if (zoom === FIT) zoomTo(2.0, e.clientX, e.clientY);
    else resetZoom();
  }, [zoom, zoomTo, resetZoom, previewReady]);

  const handlePanStart = useCallback((e) => {
    if (zoom <= FIT || e.button !== 0) return;
    e.preventDefault();
    panStart.current = { x: e.clientX, y: e.clientY };
    setIsPanning(true);
  }, [zoom]);

  // ── Edit History ─────────────────────────────────────────────────────────
  const MAX_HISTORY = 60;
  const [history, setHistory]   = useState([]);
  const [histIdx, setHistIdx]   = useState(-1); // -1 = live (no revert active)
  const isRestoring = useRef(false);

  // Label: describe what changed vs previous snapshot
  const makeLabel = (next, prev) => {
    if (!prev) return 'Initial state';
    const np = next.params;  const pp = prev.params;
    const changes = [];
    if (np.stock   !== pp.stock)   changes.push(`Stock → ${np.stock === 'none' ? 'None' : np.stock.replace(/_/g,' ')}`);
    if (np.scanner !== pp.scanner) changes.push(`Scanner → ${np.scanner}`);
    const numericKeys = Object.keys(DEFAULT_PARAMS).filter(k => 
      typeof DEFAULT_PARAMS[k] === 'number' && 
      k !== 'adaptation' &&
      k !== 'grain_strength' &&
      k !== 'grain_size' &&
      k !== 'grain_roughness'
    );
    for (const k of numericKeys) {
      if (np[k] !== pp[k]) {
        const diff = np[k] - pp[k];
        changes.push(`${k.charAt(0).toUpperCase()+k.slice(1)} ${diff > 0 ? '+' : ''}${k === 'exposure' ? diff.toFixed(2)+' EV' : diff}`);
      }
    }
    if (np.adaptation !== pp.adaptation) changes.push(`Adaptation → ${np.adaptation.toFixed(2)}`);
    if (np.grain !== pp.grain) changes.push(`Grain mode → ${np.grain}`);
    if (np.grain_strength !== pp.grain_strength && np.grain_strength !== -1.0) {
      changes.push(`Grain strength → ${np.grain_strength.toFixed(2)}`);
    }
    if (np.grain_size !== pp.grain_size && np.grain_size !== -1.0) {
      changes.push(`Grain size → ${np.grain_size.toFixed(2)}`);
    }
    if (np.grain_roughness !== pp.grain_roughness && np.grain_roughness !== -1.0) {
      changes.push(`Grain roughness → ${np.grain_roughness.toFixed(2)}`);
    }
    if (np.halation !== pp.halation) changes.push(`Halation → ${np.halation}`);
    if (next.curves !== prev.curves) changes.push('Curves adjusted');
    if (next.hsl    !== prev.hsl)    changes.push('HSL adjusted');
    return changes.length > 0 ? changes.slice(0, 3).join(', ') : 'Settings changed';
  };

  // Push to history after 700ms settle
  useEffect(() => {
    if (isRestoring.current) return; // don't re-push when we apply a history state
    if (historyDebRef.current) clearTimeout(historyDebRef.current);
    historyDebRef.current = setTimeout(() => {
      const snap = { params, curves, hsl };
      const last = lastPushedRef.current;
      // Only push if something actually changed
      if (last &&
          JSON.stringify(last.params)  === JSON.stringify(snap.params) &&
          JSON.stringify(last.curves)  === JSON.stringify(snap.curves) &&
          JSON.stringify(last.hsl)     === JSON.stringify(snap.hsl)) return;
      const label = makeLabel(snap, last);
      const entry = { id: Date.now(), label, ...snap };
      lastPushedRef.current = snap;
      setHistory(prev => [entry, ...prev].slice(0, MAX_HISTORY));
      setHistIdx(-1); // back to live
    }, 700);
    return () => clearTimeout(historyDebRef.current);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [params, curves, hsl]);

  const applyHistoryEntry = useCallback((entry, idx) => {
    isRestoring.current = true;
    setParams(entry.params);
    setCurves(entry.curves);
    setHsl(entry.hsl);
    setHistIdx(idx);
    // Let the state settle before re-enabling history tracking
    setTimeout(() => { isRestoring.current = false; }, 100);
  }, []);

  const clearHistory = useCallback(() => {
    setHistory([]);
    setHistIdx(-1);
    lastPushedRef.current = null;
  }, []);

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
      const hslStr = encodeURIComponent(JSON.stringify(hsl));
      const curvesStr = encodeURIComponent(JSON.stringify(curves));
      const hslPairs = Object.entries(hsl)
        .map(([k, v]) => `&hsl_${k}=${v}`).join('');
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
        + `&saturation=${params.saturation}`
        + `&vibrance=${params.vibrance}`
        + `&curves=${curvesStr}`
        + hslPairs
        + `&clarity=${params.clarity}`
        + `&texture=${params.texture}`
        + `&dehaze=${params.dehaze}`
        + `&bloom=${params.bloom}`
        + `&adaptation=${params.adaptation}`
        + `&grain=${encodeURIComponent(params.grain)}`
        + `&grain_strength=${params.grain_strength}`
        + `&grain_size=${params.grain_size}`
        + `&grain_roughness=${params.grain_roughness}`
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
    params.saturation, params.vibrance,
    params.clarity, params.texture, params.dehaze, params.bloom,
    params.adaptation, params.grain, params.grain_strength, params.grain_size, params.grain_roughness, params.halation,
    curves, hsl,
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
          saturation: params.saturation,
          vibrance: params.vibrance,
          curves: JSON.stringify(curves),
          ...Object.fromEntries(Object.entries(hsl).map(([k,v]) => [`hsl_${k}`, v])),
          clarity: params.clarity,
          texture: params.texture,
          dehaze: params.dehaze,
          bloom: params.bloom,
          adaptation: params.adaptation,
          grain: params.grain,
          grain_strength: params.grain_strength,
          grain_size: params.grain_size,
          grain_roughness: params.grain_roughness,
          halation: params.halation,
          export_format: exportFormat,
        }),
      });
      if (!res.ok) throw new Error(await res.text());
      const data = await res.json();
      showToast(`${data.format} saved → ${data.output_path}`, 'success');
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

  // Compare drag — only active when NOT zoomed in
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
        { key: 'temp',       label: 'Temperature', min: -100, max: 100, step: 1 },
        { key: 'tint',       label: 'Tint',        min: -100, max: 100, step: 1 },
        { key: 'vibrance',   label: 'Vibrance',    min: -100, max: 100, step: 1 },
        { key: 'saturation', label: 'Saturation',  min: -100, max: 100, step: 1 },
      ],
    },
    {
      title: 'Detail',
      rows: [
        { key: 'texture', label: 'Texture', min: -100, max: 100, step: 1 },
        { key: 'clarity', label: 'Clarity', min: -100, max: 100, step: 1 },
        { key: 'dehaze',  label: 'Dehaze',  min: -100, max: 100, step: 1 },
      ],
    },
    {
      title: 'Effects',
      rows: [
        { key: 'bloom', label: 'Film Bloom', min: 0, max: 100, step: 1 },
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
              {/* Rendering indicator */}
              {previewLoading && (
                <div style={{
                  position: 'absolute', top: 12, right: 60, zIndex: 20,
                  display: 'flex', alignItems: 'center', gap: 7,
                  background: 'rgba(0,0,0,0.55)', borderRadius: 4,
                  padding: '4px 10px', backdropFilter: 'blur(4px)',
                }}>
                  <div className="spinner" style={{ width: 12, height: 12, borderWidth: 2 }} />
                  <span style={{ fontSize: 11, color: 'rgba(255,255,255,0.6)' }}>Rendering…</span>
                </div>
              )}

              {/* Zoom toolbar */}
              <div className="zoom-toolbar">
                <button className="zoom-btn" onClick={resetZoom} title="Fit to window">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                    <path d="M8 3H5a2 2 0 0 0-2 2v3"/><path d="M21 8V5a2 2 0 0 0-2-2h-3"/>
                    <path d="M3 16v3a2 2 0 0 0 2 2h3"/><path d="M16 21h3a2 2 0 0 0 2-2v-3"/>
                  </svg>
                </button>
                <button className="zoom-btn" onClick={() => zoomTo(Math.max(FIT, zoom / 1.3))} title="Zoom out">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round"><line x1="5" y1="12" x2="19" y2="12"/></svg>
                </button>
                <span className="zoom-pct">{zoom === FIT ? 'Fit' : `${Math.round(zoom * 100)}%`}</span>
                <button className="zoom-btn" onClick={() => zoomTo(Math.min(8, zoom * 1.3))} title="Zoom in">
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round"><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>
                </button>
                <button className="zoom-btn zoom-btn--1to1" onClick={() => zoomTo(2.0)} title="1:1 pixel view">
                  <span>1:1</span>
                </button>
              </div>

              {zoom === FIT && (
                <div className="view-labels">
                  <span className="view-label">RAW</span>
                  <span className="view-label">DFEE</span>
                </div>
              )}

              {/* Zoom + pan container */}
              <div
                ref={viewerRef}
                className={`zoom-container${zoom > FIT ? ' zoomed' : ''}`}
                style={{ cursor: zoom > FIT ? (isPanning ? 'grabbing' : 'grab') : 'default' }}
                onMouseDown={handlePanStart}
                onDoubleClick={handleViewerDblClick}
              >
                <div
                  className="zoom-content"
                  style={{ transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})` }}
                >
                  {zoom === FIT ? (
                    <div className="compare-wrap" ref={containerRef}>
                      <div className="img-after">
                        <img src={previewUrl} alt="Film emulation" />
                      </div>
                      <div
                        className="img-before-wrap"
                        style={{ clipPath: `polygon(0 0, ${comparePos}% 0, ${comparePos}% 100%, 0 100%)` }}
                      >
                        <img src={rawUrl} alt="RAW sensor" />
                      </div>
                      <div
                        className="compare-divider"
                        style={{ left: `${comparePos}%` }}
                        onMouseDown={(e) => { e.stopPropagation(); setDragging(true); }}
                      >
                        <div className="compare-handle"><ArrowsIcon /></div>
                      </div>
                    </div>
                  ) : (
                    <img src={previewUrl} alt="Film emulation" style={{ display: 'block', maxWidth: '100%', maxHeight: '100%', imageRendering: 'pixelated' }} />
                  )}
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
          <div className="controls-header-bar">
            <span className="controls-header-title">Develop</span>
            <button className="reset-all-btn" onClick={handleResetAll} title="Reset all settings to defaults">
              ↺ Reset All
            </button>
          </div>
          <div className="controls-body">

            {/* Profile */}
            <div className="control-group">
              <div className="group-title collapsible" onClick={() => toggleSection('Profile')}>
                <span>Profile</span>
                <span className={`chevron ${openSections.Profile ? 'open' : ''}`}>›</span>
              </div>
              {openSections.Profile && (
                <div className="section-body">
                  <div className="field">
                    <label className="field-label">Film Stock</label>
                    <select className="select" value={params.stock} onChange={set('stock')}>
                      <option value="none">— None —</option>
                      <optgroup label="Color Negative">
                        {profiles.stocks.filter(p => p.type === 'color_negative').map(p => (
                          <option key={p.id} value={p.id}>{p.name}</option>
                        ))}
                      </optgroup>
                      <optgroup label="Color Reversal">
                        {profiles.stocks.filter(p => p.type === 'color_reversal').map(p => (
                          <option key={p.id} value={p.id}>{p.name}</option>
                        ))}
                      </optgroup>
                      <optgroup label="Monochrome">
                        {profiles.stocks.filter(p => p.type === 'monochrome').map(p => (
                          <option key={p.id} value={p.id}>{p.name}</option>
                        ))}
                      </optgroup>
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
              )}
            </div>

            {/* Curves */}
            <div className="control-group">
              <div className="group-title collapsible" onClick={() => toggleSection('Curves')}>
                <span>Curves</span>
                <span className={`chevron ${openSections.Curves ? 'open' : ''}`}>›</span>
              </div>
              {openSections.Curves && (
                <div className="section-body">
                  <CurvesPanel points={curves} onChange={setCurves} />
                </div>
              )}
            </div>

            {/* HSL */}
            <div className="control-group">
              <div className="group-title collapsible" onClick={() => toggleSection('HSL')}>
                <span>HSL</span>
                <span className={`chevron ${openSections.HSL ? 'open' : ''}`}>›</span>
              </div>
              {openSections.HSL && (
                <div className="section-body">
                  <HslPanel hsl={hsl} onChange={setHsl} />
                </div>
              )}
            </div>

            {/* Light + Color + Detail + Effects sliders */}
            {sliderGroups.map(group => (
              <div className="control-group" key={group.title}>
                <div className="group-title collapsible" onClick={() => toggleSection(group.title)}>
                  <span>{group.title}</span>
                  <span className={`chevron ${openSections[group.title] ? 'open' : ''}`}>›</span>
                </div>
                {openSections[group.title] && (
                  <div className="section-body">
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
                )}
              </div>
            ))}

            {/* Film Modifiers */}
            <div className="control-group">
              <div className="group-title collapsible" onClick={() => toggleSection('Film Modifiers')}>
                <span>Film Modifiers</span>
                <span className={`chevron ${openSections['Film Modifiers'] ? 'open' : ''}`}>›</span>
              </div>
              {openSections['Film Modifiers'] && (
                <div className="section-body">
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
                  {/* Custom Grain Controls */}
                  <div style={{ marginTop: 12, marginBottom: 12 }}>
                    <label className="checkbox-row">
                      <input
                        type="checkbox"
                        className="checkbox-input"
                        checked={params.grain === 'Auto'}
                        onChange={(e) => {
                          const isAuto = e.target.checked;
                          if (isAuto) {
                            setParams(p => ({
                              ...p,
                              grain: 'Auto',
                              grain_strength: -1.0,
                              grain_size: -1.0,
                              grain_roughness: -1.0
                            }));
                          } else {
                            setParams(p => ({
                              ...p,
                              grain: 'Custom',
                              grain_strength: 0.5,
                              grain_size: 0.6,
                              grain_roughness: 0.5
                            }));
                          }
                        }}
                      />
                      <span className="checkbox-label">Auto (ISO-Adaptive) Grain</span>
                    </label>

                    {/* Grain Strength Slider */}
                    <div className={`slider-row ${params.grain === 'Auto' ? 'disabled' : ''}`}>
                      <div className="slider-header">
                        <span className="slider-label">Grain Strength</span>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          {params.grain !== 'Auto' && params.grain_strength !== 0.5 && (
                            <button
                              className="revert-btn"
                              title="Reset Strength to default"
                              onClick={() => setParams(p => ({ ...p, grain_strength: 0.5 }))}
                            >↺</button>
                          )}
                          <span className={`slider-value ${params.grain !== 'Auto' && params.grain_strength !== 0.5 ? 'slider-value--dirty' : ''}`}>
                            {params.grain === 'Auto' ? 'Auto' : params.grain_strength.toFixed(2)}
                          </span>
                        </div>
                      </div>
                      <input
                        type="range" min={0.0} max={2.0} step={0.05}
                        value={params.grain_strength === -1.0 ? 0.5 : params.grain_strength}
                        onChange={(e) => {
                          const val = parseFloat(e.target.value);
                          setParams(p => ({ ...p, grain_strength: val }));
                        }}
                        disabled={params.grain === 'Auto'}
                        className={`slider ${params.grain !== 'Auto' && params.grain_strength !== 0.5 ? 'slider--dirty' : ''}`}
                      />
                    </div>

                    {/* Grain Size Slider */}
                    <div className={`slider-row ${params.grain === 'Auto' ? 'disabled' : ''}`} style={{ marginTop: 8 }}>
                      <div className="slider-header">
                        <span className="slider-label">Grain Size</span>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          {params.grain !== 'Auto' && params.grain_size !== 0.6 && (
                            <button
                              className="revert-btn"
                              title="Reset Size to default"
                              onClick={() => setParams(p => ({ ...p, grain_size: 0.6 }))}
                            >↺</button>
                          )}
                          <span className={`slider-value ${params.grain !== 'Auto' && params.grain_size !== 0.6 ? 'slider-value--dirty' : ''}`}>
                            {params.grain === 'Auto' ? 'Auto' : params.grain_size.toFixed(2)}
                          </span>
                        </div>
                      </div>
                      <input
                        type="range" min={0.1} max={2.0} step={0.05}
                        value={params.grain_size === -1.0 ? 0.6 : params.grain_size}
                        onChange={(e) => {
                          const val = parseFloat(e.target.value);
                          setParams(p => ({ ...p, grain_size: val }));
                        }}
                        disabled={params.grain === 'Auto'}
                        className={`slider ${params.grain !== 'Auto' && params.grain_size !== 0.6 ? 'slider--dirty' : ''}`}
                      />
                    </div>

                    {/* Grain Roughness Slider */}
                    <div className={`slider-row ${params.grain === 'Auto' ? 'disabled' : ''}`} style={{ marginTop: 8 }}>
                      <div className="slider-header">
                        <span className="slider-label">Grain Roughness / Crispness</span>
                        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                          {params.grain !== 'Auto' && params.grain_roughness !== 0.5 && (
                            <button
                              className="revert-btn"
                              title="Reset Roughness to default"
                              onClick={() => setParams(p => ({ ...p, grain_roughness: 0.5 }))}
                            >↺</button>
                          )}
                          <span className={`slider-value ${params.grain !== 'Auto' && params.grain_roughness !== 0.5 ? 'slider-value--dirty' : ''}`}>
                            {params.grain === 'Auto' ? 'Auto' : params.grain_roughness.toFixed(2)}
                          </span>
                        </div>
                      </div>
                      <input
                        type="range" min={0.0} max={1.0} step={0.05}
                        value={params.grain_roughness === -1.0 ? 0.5 : params.grain_roughness}
                        onChange={(e) => {
                          const val = parseFloat(e.target.value);
                          setParams(p => ({ ...p, grain_roughness: val }));
                        }}
                        disabled={params.grain === 'Auto'}
                        className={`slider ${params.grain !== 'Auto' && params.grain_roughness !== 0.5 ? 'slider--dirty' : ''}`}
                      />
                    </div>
                  </div>
                  <div className="field">
                    <label className="field-label">Halation</label>
                    <select className="select" value={params.halation} onChange={set('halation')}>
                      {['Auto','Off','Low','Medium','High'].map(v => <option key={v}>{v}</option>)}
                    </select>
                  </div>
                </div>
              )}
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

            {/* History */}
            {history.length > 0 && (
              <div className="control-group">
                <div className="group-title collapsible" onClick={() => toggleSection('History')}>
                  <span>History</span>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                    <span className="history-count">{history.length}</span>
                    <span className={`chevron ${openSections.History ? 'open' : ''}`}>›</span>
                  </div>
                </div>
                {openSections['History'] && (
                  <div className="section-body">
                    <div className="history-list">
                      <div
                        className={`history-entry${histIdx === -1 ? ' history-entry--active' : ''}`}
                        onClick={() => { if (histIdx !== -1 && history.length > 0) applyHistoryEntry(history[0], -1); }}
                        style={{ fontStyle: 'italic', opacity: histIdx === -1 ? 1 : 0.6 }}
                      >
                        <span className="history-dot history-dot--live" />
                        <span className="history-label">Current state</span>
                      </div>
                      {history.map((entry, idx) => (
                        <div
                          key={entry.id}
                          className={`history-entry${histIdx === idx ? ' history-entry--active' : ''}`}
                          onClick={() => applyHistoryEntry(entry, idx)}
                          title={new Date(entry.id).toLocaleTimeString()}
                        >
                          <span className="history-dot" />
                          <div className="history-entry-content">
                            <span className="history-label">{entry.label}</span>
                            <span className="history-time">{new Date(entry.id).toLocaleTimeString([], { hour:'2-digit', minute:'2-digit', second:'2-digit' })}</span>
                          </div>
                        </div>
                      ))}
                    </div>
                    <button className="history-clear-btn" onClick={clearHistory}>Clear history</button>
                  </div>
                )}
              </div>
            )}
          </div>

          {/* Export */}
          {selectedFile && (
            <div className="controls-footer">
              {/* Format picker */}
              <div className="export-format-row">
                <span className="export-format-label">Format</span>
                <div className="export-format-pills">
                  {[
                    { id: 'png8',  label: 'PNG',      sub: '8-bit'  },
                    { id: 'png16', label: 'PNG',      sub: '16-bit' },
                    { id: 'tiff',  label: 'TIFF',     sub: '16-bit' },
                  ].map(({ id, label, sub }) => (
                    <button
                      key={id}
                      className={`format-pill${exportFormat === id ? ' active' : ''}`}
                      onClick={() => setExportFormat(id)}
                    >
                      <span className="format-pill-name">{label}</span>
                      <span className="format-pill-sub">{sub}</span>
                    </button>
                  ))}
                </div>
              </div>
              <button className="export-btn" onClick={handleExport} disabled={exporting || !previewReady}>
                {exporting
                  ? <><div className="spinner" style={{ width: 14, height: 14, borderWidth: 2 }} /> Exporting…</>
                  : `Export ${exportFormat === 'png8' ? '8-bit PNG' : exportFormat === 'png16' ? '16-bit PNG' : '16-bit TIFF'}`}
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
            <p>Exporting full resolution…</p>
            <span>
              {exportFormat === 'png8'  ? 'Ingesting RAW · Rendering · Saving 8-bit PNG'
             : exportFormat === 'png16' ? 'Ingesting RAW · Rendering · Saving 16-bit PNG'
             :                            'Ingesting RAW · Rendering · Saving 16-bit TIFF'}
            </span>
            <span style={{ fontSize: 10, opacity: 0.45, marginTop: 4 }}>
              This takes 30–90s — full-res RAW must be re-rendered
            </span>
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
