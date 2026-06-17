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
  sharpness: 0.0,
  sharpness_mask: 0.5,
  film_color: 100,
  print_stock: 'none',
  print_strength: 1.0,
  print_c: 0.0,
  print_m: 0.0,
  print_y: 0.0,
  print_contrast: 0.0,
  print_black_point: 0.0,
};

const HSL_RANGES_KEYS = ['red','orange','yellow','green','aqua','blue','purple','magenta'];
const DEFAULT_HSL = Object.fromEntries(
  HSL_RANGES_KEYS.flatMap(r => ['h','s','l'].map(p => [`${r}_${p}`, 0]))
);

const BUILTIN_PRESETS = [
  {
    id: 'k64',
    name: 'Kodachrome 64 Soft',
    stock: 'kodachrome_64',
    params: { exposure: 0.15, contrast: 20, temp: 15, tint: 5, adaptation: 1.0 },
    curves: DEFAULT_CURVES,
    hsl: DEFAULT_HSL,
    isBuiltin: true
  },
  {
    id: 'portra',
    name: 'Portra 400 Soft Warm',
    stock: 'portra_400',
    params: { exposure: 0.4, contrast: -10, temp: 5, tint: -10, adaptation: 0.85 },
    curves: DEFAULT_CURVES,
    hsl: DEFAULT_HSL,
    isBuiltin: true
  },
  {
    id: 'trix',
    name: 'Tri-X 400 Rich Black',
    stock: 'tri_x_400',
    params: { exposure: 0.0, contrast: 30, temp: 0, tint: 0, adaptation: 1.0 },
    curves: DEFAULT_CURVES,
    hsl: DEFAULT_HSL,
    isBuiltin: true
  },
  {
    id: 'velvia',
    name: 'Velvia 50 Landscape',
    stock: 'velvia_50',
    params: { exposure: -0.10, contrast: 15, temp: 5, tint: 5, saturation: 10, adaptation: 1.0 },
    curves: DEFAULT_CURVES,
    hsl: DEFAULT_HSL,
    isBuiltin: true
  }
];

export default function App() {
  const [files, setFiles] = useState([]);
  const [profiles, setProfiles] = useState({ stocks: [], print_stocks: [] });
  const [selectedFile, setSelectedFile] = useState(null);
  const [loadingFiles, setLoadingFiles] = useState(false);
  const [selectLoading, setSelectLoading] = useState(false);
  const [diagnostics, setDiagnostics] = useState(null);
  const [params, setParams] = useState(DEFAULT_PARAMS);
  const [curves, setCurves] = useState(DEFAULT_CURVES);
  const [hsl, setHsl] = useState(DEFAULT_HSL);
  const [metadata, setMetadata] = useState(null);
  const [showExif, setShowExif] = useState(true);

  // ── Presets states ──────────────────────────────────────────────────────
  const [userPresets, setUserPresets] = useState(() => {
    try {
      const saved = localStorage.getItem('dfee_user_presets');
      return saved ? JSON.parse(saved) : [];
    } catch { return []; }
  });
  const [newPresetName, setNewPresetName] = useState('');
  const [filterByStock, setFilterByStock] = useState(true);

  const getAsShotWB = () => {
    if (!metadata || !metadata.white_balance_multipliers) return { temp: 5500, tint: 10 };
    try {
      const wbm = typeof metadata.white_balance_multipliers === 'string'
        ? JSON.parse(metadata.white_balance_multipliers)
        : metadata.white_balance_multipliers;
      if (wbm && wbm.length >= 3) {
        const r_mul = wbm[0];
        const g_mul = (wbm[1] + (wbm[3] || wbm[1])) / 2.0 || 1.0;
        const b_mul = wbm[2];
        
        // Normalize multipliers relative to green
        const r_norm = r_mul / g_mul;
        const b_norm = b_mul / g_mul;
        
        const ratio = r_norm / (b_norm || 1.0);
        const temp = Math.round(4668.0 * Math.pow(ratio, 0.578));
        const tint = Math.round(((r_norm + b_norm) / 2.0 - 1.75) * 30);
        return { temp, tint };
      }
    } catch (e) {
      console.error(e);
    }
    return { temp: 5500, tint: 10 };
  };

  const { temp: T_as_shot, tint: tint_as_shot } = getAsShotWB();

  const formatShutterSpeed = (ss, ssStr) => {
    if (ssStr) return ssStr;
    if (!ss) return '';
    const val = parseFloat(ss);
    if (isNaN(val)) return ss;
    if (val >= 1) {
      return `${val.toFixed(1).replace(/\.0$/, '')}"`;
    }
    const reciprocal = Math.round(1 / val);
    return `1/${reciprocal}s`;
  };

  const formatFocalLength = (fl) => {
    if (!fl || fl === 'None') return '';
    const val = parseFloat(fl);
    if (isNaN(val)) return fl;
    return `${Math.round(val)}mm`;
  };

  const formatAperture = (ap) => {
    if (!ap || ap === 'None') return '';
    const val = parseFloat(ap);
    if (isNaN(val)) return ap;
    return `f/${val.toFixed(1).replace(/\.0$/, '')}`;
  };

  const formatMegapixels = (width, height) => {
    if (!width || !height) return '';
    const w = parseInt(width, 10);
    const h = parseInt(height, 10);
    if (isNaN(w) || isNaN(h)) return '';
    return `${((w * h) / 1000000).toFixed(1)}MP`;
  };

  useEffect(() => {
    const handleKeyDown = (e) => {
      if (['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName)) {
        return;
      }
      if (e.key === 'i' || e.key === 'I') {
        setShowExif(v => !v);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

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
  const rawAbortRef = useRef(null);
  const previewAbortRef = useRef(null);
  const selectTokenRef = useRef(0);
  const previewRequestKeyRef = useRef('');
  const loadedPreviewKeyRef = useRef('');
  const rawObjectUrlRef = useRef('');
  const previewObjectUrlRef = useRef('');
  const historyDebRef  = useRef(null);
  const lastPushedRef  = useRef(null);  // snapshot of last history entry pushed
  const histogramCanvasRef = useRef(null);

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

  const revokeObjectUrl = useCallback((urlRef) => {
    if (urlRef.current) {
      URL.revokeObjectURL(urlRef.current);
      urlRef.current = '';
    }
  }, []);

  const replaceObjectUrl = useCallback((urlRef, blob, setUrl) => {
    revokeObjectUrl(urlRef);
    const nextUrl = URL.createObjectURL(blob);
    urlRef.current = nextUrl;
    setUrl(nextUrl);
    return nextUrl;
  }, [revokeObjectUrl]);

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

  useEffect(() => () => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    if (rawAbortRef.current) rawAbortRef.current.abort();
    if (previewAbortRef.current) previewAbortRef.current.abort();
    revokeObjectUrl(rawObjectUrlRef);
    revokeObjectUrl(previewObjectUrlRef);
  }, [revokeObjectUrl]);

  // Label: describe what changed vs previous snapshot
  const makeLabel = (next, prev) => {
    if (!prev) return 'Initial state';
    const np = next.params;  const pp = prev.params;
    const changes = [];
    if (np.stock   !== pp.stock)   changes.push(`Stock → ${np.stock === 'none' ? 'None' : np.stock.replace(/_/g,' ')}`);

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
      ? (['exposure', 'adaptation', 'sharpness', 'sharpness_mask'].includes(key) ? parseFloat(e.target.value) : parseInt(e.target.value))
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
    const selectToken = selectTokenRef.current + 1;
    selectTokenRef.current = selectToken;
    setSelectLoading(true);
    setPreviewReady(false);
    setPreviewLoading(false);
    setSelectedFile(null);
    setPreviewUrl('');
    setRawUrl('');
    previewRequestKeyRef.current = '';
    loadedPreviewKeyRef.current = '';
    if (debounceRef.current) clearTimeout(debounceRef.current);
    if (previewAbortRef.current) {
      previewAbortRef.current.abort();
      previewAbortRef.current = null;
    }
    if (rawAbortRef.current) {
      rawAbortRef.current.abort();
      rawAbortRef.current = null;
    }
    revokeObjectUrl(previewObjectUrlRef);
    revokeObjectUrl(rawObjectUrlRef);
    try {
      const res = await fetch(`${API}/api/select`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ filename }),
      });
      if (!res.ok) throw new Error(await res.text());
      const data = await res.json();
      if (selectTokenRef.current !== selectToken) return;
      setDiagnostics(data.diagnostics);
      setMetadata(data.metadata);
      const rawController = new AbortController();
      rawAbortRef.current = rawController;
      const rawResponse = await fetch(`${API}/api/raw-image`, {
        signal: rawController.signal,
        cache: 'no-store',
      });
      if (!rawResponse.ok) throw new Error(await rawResponse.text());
      const rawBlob = await rawResponse.blob();
      if (selectTokenRef.current !== selectToken || rawAbortRef.current !== rawController) return;
      replaceObjectUrl(rawObjectUrlRef, rawBlob, setRawUrl);
      rawAbortRef.current = null;
      // Set selectedFile AFTER server confirms session is ready
      // This prevents the preview useEffect from firing before the session exists
      setSelectedFile(filename);
    } catch (e) {
      if (e.name === 'AbortError') return;
      showToast(`Load failed: ${e.message}`, 'error');
    } finally {
      if (selectTokenRef.current === selectToken) {
        setSelectLoading(false);
      }
    }
  };

  // Debounced preview — URL built INSIDE effect so params are never stale
  useEffect(() => {
    if (!selectedFile) return;

    if (params.stock === 'none') {
      if (previewAbortRef.current) {
        previewAbortRef.current.abort();
        previewAbortRef.current = null;
      }
      previewRequestKeyRef.current = '';
      loadedPreviewKeyRef.current = '';
      revokeObjectUrl(previewObjectUrlRef);
      setPreviewUrl(rawUrl);
      setPreviewReady(!!rawUrl);
      setPreviewLoading(false);
      return;
    }

    if (debounceRef.current) clearTimeout(debounceRef.current);

    debounceRef.current = setTimeout(() => {
      const requestKey = JSON.stringify({ filename: selectedFile, params, curves, hsl });
      if (requestKey === loadedPreviewKeyRef.current && previewUrl) {
        setPreviewReady(true);
        setPreviewLoading(false);
        return;
      }
      if (requestKey === previewRequestKeyRef.current) {
        return;
      }

      const query = new URLSearchParams({
        filename: selectedFile,
        stock: params.stock,
        exposure: String(params.exposure),
        highlights: String(params.highlights),
        shadows: String(params.shadows),
        blacks: String(params.blacks),
        whites: String(params.whites),
        midtones: String(params.midtones),
        contrast: String(params.contrast),
        temp: String(params.temp),
        tint: String(params.tint),
        saturation: String(params.saturation),
        vibrance: String(params.vibrance),
        curves: JSON.stringify(curves),
        clarity: String(params.clarity),
        texture: String(params.texture),
        dehaze: String(params.dehaze),
        bloom: String(params.bloom),
        adaptation: String(params.adaptation),
        grain: params.grain,
        grain_strength: String(params.grain_strength),
        grain_size: String(params.grain_size),
        grain_roughness: String(params.grain_roughness),
        halation: params.halation,
        sharpness: String(params.sharpness),
        sharpness_mask: String(params.sharpness_mask),
        film_color: String(params.film_color),
        print_stock: params.print_stock,
        print_strength: String(params.print_strength),
        print_c: String(params.print_c),
        print_m: String(params.print_m),
        print_y: String(params.print_y),
        print_contrast: String(params.print_contrast),
        print_black_point: String(params.print_black_point),
      });
      Object.entries(hsl).forEach(([key, value]) => {
        query.set(`hsl_${key}`, String(value));
      });
      const requestUrl = `${API}/api/preview?${query.toString()}`;

      if (previewAbortRef.current) {
        previewAbortRef.current.abort();
      }
      const controller = new AbortController();
      previewAbortRef.current = controller;
      previewRequestKeyRef.current = requestKey;
      setPreviewLoading(true);

      fetch(requestUrl, {
        signal: controller.signal,
        cache: 'no-store',
      })
        .then(async (res) => {
          if (!res.ok) throw new Error(await res.text());
          return res.blob();
        })
        .then((blob) => {
          if (previewAbortRef.current !== controller) return;
          replaceObjectUrl(previewObjectUrlRef, blob, setPreviewUrl);
          loadedPreviewKeyRef.current = requestKey;
          previewRequestKeyRef.current = '';
          previewAbortRef.current = null;
          setPreviewReady(true);
          setPreviewLoading(false);
        })
        .catch((e) => {
          if (e.name === 'AbortError') return;
          if (previewAbortRef.current === controller) {
            previewRequestKeyRef.current = '';
            previewAbortRef.current = null;
            setPreviewLoading(false);
          }
          showToast('Preview render failed â€” check server terminal for details', 'error');
        });
      return;

      /* Legacy image-loader path retained temporarily for reference.
      const hslStr = encodeURIComponent(JSON.stringify(hsl));
      const curvesStr = encodeURIComponent(JSON.stringify(curves));
      const hslPairs = Object.entries(hsl)
        .map(([k, v]) => `&hsl_${k}=${v}`).join('');
      const url = `${API}/api/preview?filename=${encodeURIComponent(selectedFile)}`
        + `&stock=${encodeURIComponent(params.stock)}`

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
        + `&sharpness=${params.sharpness}`
        + `&sharpness_mask=${params.sharpness_mask}`
        + `&film_color=${params.film_color}`
        + `&print_stock=${params.print_stock}`
        + `&print_strength=${params.print_strength}`
        + `&print_c=${params.print_c}`
        + `&print_m=${params.print_m}`
        + `&print_y=${params.print_y}`
        + `&print_contrast=${params.print_contrast}`
        + `&print_black_point=${params.print_black_point}`
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
      */
    }, 200);

    return () => clearTimeout(debounceRef.current);
  }, [
    selectedFile, rawUrl, previewUrl,
    params.stock, params.exposure, params.highlights,
    params.shadows, params.blacks, params.whites, params.midtones,
    params.contrast, params.temp, params.tint,
    params.saturation, params.vibrance,
    params.clarity, params.texture, params.dehaze, params.bloom,
    params.adaptation, params.grain, params.grain_strength, params.grain_size, params.grain_roughness, params.halation,
    params.sharpness, params.sharpness_mask, params.film_color,
    params.print_stock, params.print_strength,
    params.print_c, params.print_m, params.print_y,
    params.print_contrast, params.print_black_point,
    curves, hsl, replaceObjectUrl, revokeObjectUrl, showToast,
  ]);

  useEffect(() => {
    if (!previewUrl) return;

    const img = new Image();
    img.crossOrigin = "anonymous";
    img.onload = () => {
      const canvas = histogramCanvasRef.current;
      if (!canvas) return;

      const offscreen = document.createElement('canvas');
      const ctxOff = offscreen.getContext('2d');
      if (!ctxOff) return;

      const analysisSize = 128;
      offscreen.width = analysisSize;
      offscreen.height = analysisSize;
      ctxOff.drawImage(img, 0, 0, analysisSize, analysisSize);

      try {
        const imgData = ctxOff.getImageData(0, 0, analysisSize, analysisSize);
        const data = imgData.data;

        const rHist = new Array(256).fill(0);
        const gHist = new Array(256).fill(0);
        const bHist = new Array(256).fill(0);
        const lumaHist = new Array(256).fill(0);

        for (let i = 0; i < data.length; i += 4) {
          const r = data[i];
          const g = data[i + 1];
          const b = data[i + 2];
          const luma = Math.round(0.2126 * r + 0.7152 * g + 0.0722 * b);

          rHist[r]++;
          gHist[g]++;
          bHist[b]++;
          lumaHist[luma]++;
        }

        const width = canvas.width;
        const height = canvas.height;
        const ctx = canvas.getContext('2d');
        if (!ctx) return;

        ctx.clearRect(0, 0, width, height);

        ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
        ctx.lineWidth = 1;
        for (let x = width / 4; x < width; x += width / 4) {
          ctx.beginPath();
          ctx.moveTo(x, 0);
          ctx.lineTo(x, height);
          ctx.stroke();
        }

        const smooth = (arr) => {
          const smoothed = [...arr];
          for (let iter = 0; iter < 2; iter++) {
            for (let i = 1; i < 255; i++) {
              smoothed[i] = (smoothed[i - 1] + smoothed[i] + smoothed[i + 1]) / 3;
            }
          }
          return smoothed;
        };

        const rSmooth = smooth(rHist);
        const gSmooth = smooth(gHist);
        const bSmooth = smooth(bHist);
        const lSmooth = smooth(lumaHist);

        let maxVal = 1;
        for (let i = 2; i < 254; i++) {
          maxVal = Math.max(maxVal, rSmooth[i], gSmooth[i], bSmooth[i], lSmooth[i]);
        }

        const drawChannel = (hist, color) => {
          ctx.beginPath();
          ctx.moveTo(0, height);
          for (let i = 0; i < 256; i++) {
            const x = (i / 255) * width;
            const normalizedVal = Math.min(hist[i] / maxVal, 1.0);
            const y = height - normalizedVal * (height - 4);
            ctx.lineTo(x, y);
          }
          ctx.lineTo(width, height);
          ctx.closePath();
          ctx.fillStyle = color;
          ctx.fill();
        };

        ctx.globalCompositeOperation = 'screen';
        drawChannel(rSmooth, 'rgba(235, 77, 75, 0.55)');
        drawChannel(gSmooth, 'rgba(76, 175, 80, 0.55)');
        drawChannel(bSmooth, 'rgba(58, 123, 213, 0.55)');
        
        ctx.globalCompositeOperation = 'source-over';
        ctx.beginPath();
        for (let i = 0; i < 256; i++) {
          const x = (i / 255) * width;
          const normalizedVal = Math.min(lSmooth[i] / maxVal, 1.0);
          const y = height - normalizedVal * (height - 4);
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.35)';
        ctx.lineWidth = 1.5;
        ctx.stroke();

      } catch (err) {
        console.error("Histogram error:", err);
      }
    };
    img.src = previewUrl;
  }, [previewUrl]);

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
          sharpness: params.sharpness,
          sharpness_mask: params.sharpness_mask,
          film_color: params.film_color,
          print_stock: params.print_stock,
          print_strength: params.print_strength,
          print_c: params.print_c,
          print_m: params.print_m,
          print_y: params.print_y,
          print_contrast: params.print_contrast,
          print_black_point: params.print_black_point,
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

  const applyPreset = (preset) => {
    setParams(p => ({ ...DEFAULT_PARAMS, ...p, ...preset.params, stock: preset.stock }));
    if (preset.curves) setCurves(preset.curves);
    if (preset.hsl) setHsl(preset.hsl);
    showToast(`Applied preset "${preset.name}"`, 'success');
  };

  const savePreset = () => {
    if (!newPresetName.trim()) {
      showToast('Please enter a preset name', 'warning');
      return;
    }
    const name = newPresetName.trim();
    if (userPresets.some(p => p.name.toLowerCase() === name.toLowerCase()) || 
        BUILTIN_PRESETS.some(p => p.name.toLowerCase() === name.toLowerCase())) {
      showToast('A preset with this name already exists', 'warning');
      return;
    }
    const newPreset = {
      id: 'user_' + Date.now(),
      name: name,
      stock: params.stock,
      params: { ...params },
      curves: [...curves],
      hsl: { ...hsl },
      isBuiltin: false
    };
    const updated = [...userPresets, newPreset];
    setUserPresets(updated);
    try {
      localStorage.setItem('dfee_user_presets', JSON.stringify(updated));
    } catch (e) {
      console.error('Failed to save user presets', e);
    }
    setNewPresetName('');
    showToast(`Preset "${name}" saved`, 'success');
  };

  const deletePreset = (id, e) => {
    e.stopPropagation();
    const updated = userPresets.filter(p => p.id !== id);
    setUserPresets(updated);
    try {
      localStorage.setItem('dfee_user_presets', JSON.stringify(updated));
    } catch (e) {
      console.error('Failed to update user presets', e);
    }
    showToast('Preset deleted', 'info');
  };

  const allPresets = [...BUILTIN_PRESETS, ...userPresets];
  const filteredPresets = filterByStock
    ? allPresets.filter(p => p.stock === params.stock)
    : allPresets;

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
    if (key === 'adaptation' || key === 'sharpness' || key === 'sharpness_mask') return v.toFixed(2);
    if (key === 'temp') {
      const currentTemp = Math.max(2000, Math.min(20000, Math.round(T_as_shot + v * 80)));
      return currentTemp + ' K';
    }
    if (key === 'tint') {
      const currentTint = Math.max(-150, Math.min(150, Math.round(tint_as_shot + v * 2)));
      return (currentTint > 0 ? '+' : '') + currentTint;
    }
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
        { key: 'sharpness', label: 'Detail Sharpness', min: 0.0, max: 2.0, step: 0.05 },
        { key: 'sharpness_mask', label: 'Luminance Mask', min: 0.0, max: 1.0, step: 0.05 },
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
            <div className="sidebar-presets-panel">
              <div className="presets-panel-header">
                <span className="presets-label">Presets</span>
                <label className="presets-filter-toggle" title="Filter presets to match current film stock">
                  <input
                    type="checkbox"
                    checked={filterByStock}
                    onChange={(e) => setFilterByStock(e.target.checked)}
                  />
                  <span>Match Stock</span>
                </label>
              </div>

              {/* Save preset form */}
              <div className="preset-save-bar">
                <input
                  type="text"
                  className="preset-name-input"
                  placeholder="New preset name..."
                  value={newPresetName}
                  onChange={(e) => setNewPresetName(e.target.value)}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter') savePreset();
                  }}
                />
                <button className="preset-save-btn" onClick={savePreset} title="Save current settings as preset">
                  Save
                </button>
              </div>

              {/* Presets List */}
              <div className="preset-list">
                {filteredPresets.length === 0 ? (
                  <div className="preset-list-empty">
                    No presets found.
                  </div>
                ) : (
                  filteredPresets.map(preset => {
                    const stockObj = profiles.stocks.find(s => s.id === preset.stock);
                    const stockName = stockObj ? stockObj.name.replace(/Kodak |Fujifilm |Fuji /g, '') : preset.stock;
                    return (
                      <div
                        key={preset.id}
                        className={`preset-item ${preset.isBuiltin ? 'builtin' : 'user'}`}
                        onClick={() => applyPreset(preset)}
                      >
                        <div className="preset-info">
                          <span className="preset-item-name">{preset.name}</span>
                          <span className={`preset-stock-badge ${preset.stock}`}>
                            {stockName}
                          </span>
                        </div>
                        {!preset.isBuiltin && (
                          <button
                            className="preset-delete-btn"
                            onClick={(e) => deletePreset(preset.id, e)}
                            title="Delete preset"
                          >
                            ✕
                          </button>
                        )}
                      </div>
                    );
                  })
                )}
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
                <button
                  className={`zoom-btn ${showExif ? 'zoom-btn--active' : ''}`}
                  onClick={() => setShowExif(v => !v)}
                  title="Toggle EXIF Metadata Overlay (Key: I)"
                  style={{ marginLeft: 4 }}
                >
                  <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
                    <circle cx="12" cy="12" r="10"/>
                    <line x1="12" y1="16" x2="12" y2="12"/>
                    <line x1="12" y1="8" x2="12.01" y2="8"/>
                  </svg>
                </button>
              </div>

              {zoom === FIT && (
                <div className="view-labels">
                  <span className="view-label">RAW</span>
                  <span className="view-label">DFEE</span>
                </div>
              )}

              {showExif && metadata && (
                <div className="exif-badge">
                  <div className="exif-camera">
                    {(() => {
                      let text = '';
                      if (metadata.camera_make && metadata.camera_make !== 'Unknown') text += metadata.camera_make + ' ';
                      if (metadata.camera_model && metadata.camera_model !== 'Unknown') text += metadata.camera_model;
                      return text.trim() || 'Unknown Camera';
                    })()}
                  </div>
                  <div className="exif-details">
                    {[
                      metadata.lens_model && metadata.lens_model !== 'Unknown' && metadata.lens_model !== 'None' ? metadata.lens_model : null,
                      formatFocalLength(metadata.focal_length),
                      formatAperture(metadata.aperture),
                      formatShutterSpeed(metadata.shutter_speed, metadata.shutter_speed_str),
                      metadata.iso ? `ISO ${metadata.iso}` : null
                    ].filter(Boolean).join('   •   ')}
                    {metadata.image_width && metadata.image_height && (
                      <span className="exif-dims">
                        {'   •   '}{formatMegapixels(metadata.image_width, metadata.image_height)} ({metadata.image_width} × {metadata.image_height})
                      </span>
                    )}
                  </div>
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
            <div className="histogram-container">
              <canvas
                ref={histogramCanvasRef}
                width={260}
                height={120}
                className="histogram-canvas"
              />
            </div>

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
                    <label className="field-label" title="Applies a theatrical positive print stock emulation on top of the camera negative, adding characteristic shadow lift and contrast.">
                      Print Finish
                    </label>
                    <select className="select" value={params.print_stock} onChange={set('print_stock')}>
                      {profiles.print_stocks.map(p => (
                        <option key={p.id} value={p.id}>{p.name}</option>
                      ))}
                    </select>
                  </div>
                  {params.print_stock !== 'none' && (
                    <div style={{ marginTop: 8, padding: '12px 10px', backgroundColor: '#181818', borderRadius: 6, border: '1px solid #282828' }}>
                      <div style={{ fontSize: 11, fontWeight: 600, color: '#888', textTransform: 'uppercase', marginBottom: 12, letterSpacing: 0.5 }}>
                        Print Controls
                      </div>
                      <div className="field" style={{ marginBottom: 16 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label">Print Strength</span>
                          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                            {params.print_strength !== DEFAULT_PARAMS.print_strength && (
                              <button
                                className="revert-btn"
                                onClick={() => setParams(p => ({ ...p, print_strength: DEFAULT_PARAMS.print_strength }))}
                              >↺</button>
                            )}
                            <span className={`slider-value${params.print_strength !== DEFAULT_PARAMS.print_strength ? ' slider-value--dirty' : ''}`}>
                              {Math.round(params.print_strength * 100)}%
                            </span>
                          </div>
                        </div>
                        <input type="range" min="0" max="2" step="0.05"
                          value={params.print_strength} onChange={set('print_strength')}
                          className={`slider slider-print-strength${params.print_strength !== DEFAULT_PARAMS.print_strength ? ' slider--dirty' : ''}`}
                        />
                      </div>
                      
                      <div className="field" style={{ marginBottom: 16 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label" title="Subtractive Cyan (-Red)">Color Head: Cyan</span>
                          <span className="slider-value">{params.print_c}</span>
                        </div>
                        <input type="range" min="-100" max="100"
                          value={params.print_c} onChange={set('print_c')}
                          className="slider slider-cyan"
                        />
                      </div>
                      <div className="field" style={{ marginBottom: 16 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label" title="Subtractive Magenta (-Green)">Color Head: Magenta</span>
                          <span className="slider-value">{params.print_m}</span>
                        </div>
                        <input type="range" min="-100" max="100"
                          value={params.print_m} onChange={set('print_m')}
                          className="slider slider-magenta"
                        />
                      </div>
                      <div className="field" style={{ marginBottom: 16 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label" title="Subtractive Yellow (-Blue)">Color Head: Yellow</span>
                          <span className="slider-value">{params.print_y}</span>
                        </div>
                        <input type="range" min="-100" max="100"
                          value={params.print_y} onChange={set('print_y')}
                          className="slider slider-yellow"
                        />
                      </div>

                      <div className="field" style={{ marginBottom: 16 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label" title="Modulates the print S-curve steepness">Print Contrast</span>
                          <span className="slider-value">{params.print_contrast}</span>
                        </div>
                        <input type="range" min="-100" max="100"
                          value={params.print_contrast} onChange={set('print_contrast')}
                          className="slider"
                        />
                      </div>

                      <div className="field" style={{ marginBottom: 4 }}>
                        <div className="slider-header" style={{ marginBottom: 4 }}>
                          <span className="slider-label" title="Modulates the print base D-Min density">Black Point (Lift)</span>
                          <span className="slider-value">{params.print_black_point}</span>
                        </div>
                        <input type="range" min="-100" max="100"
                          value={params.print_black_point} onChange={set('print_black_point')}
                          className="slider"
                        />
                      </div>
                    </div>
                  )}
                  <div className="field" style={{ marginTop: 14 }}>
                    <div className="slider-header" style={{ marginBottom: 4 }}>
                      <span className="slider-label">Auto-Compensation (Adaptation)</span>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                        {params.adaptation !== DEFAULT_PARAMS.adaptation && (
                          <button
                            className="revert-btn"
                            title="Reset Auto-Compensation to default"
                            onClick={() => setParams(p => ({ ...p, adaptation: DEFAULT_PARAMS.adaptation }))}
                          >↺</button>
                        )}
                        <span className={`slider-value${params.adaptation !== DEFAULT_PARAMS.adaptation ? ' slider-value--dirty' : ''}`}>
                          {Math.round(params.adaptation * 100)}%
                        </span>
                      </div>
                    </div>
                    <input
                      type="range" min={0} max={1.5} step={0.05}
                      value={params.adaptation} onChange={set('adaptation')}
                      className={`slider${params.adaptation !== DEFAULT_PARAMS.adaptation ? ' slider--dirty' : ''}`}
                    />
                  </div>
                  <div className="field" style={{ marginTop: 14 }}>
                    <div className="slider-header" style={{ marginBottom: 4 }}>
                      <span className="slider-label">Film Color</span>
                      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                        {params.film_color !== DEFAULT_PARAMS.film_color && (
                          <button
                            className="revert-btn"
                            title="Reset Film Color to default"
                            onClick={() => setParams(p => ({ ...p, film_color: DEFAULT_PARAMS.film_color }))}
                          >↺</button>
                        )}
                        <span className={`slider-value${params.film_color !== DEFAULT_PARAMS.film_color ? ' slider-value--dirty' : ''}`}>
                          {Math.round(params.film_color)}
                        </span>
                      </div>
                    </div>
                    <input
                      type="range" min={0} max={200} step={5}
                      value={params.film_color} onChange={set('film_color')}
                      className={`slider slider-film-color${params.film_color !== DEFAULT_PARAMS.film_color ? ' slider--dirty' : ''}`}
                    />
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
                      
                      let sliderMin = min;
                      let sliderMax = max;
                      let sliderStep = step;
                      let sliderVal = params[key];
                      let sliderOnChange = set(key);
                      let sliderClass = `slider${isDirty ? ' slider--dirty' : ''}`;

                      if (key === 'temp') {
                        sliderMin = 2000;
                        sliderMax = 20000;
                        sliderStep = 50;
                        sliderVal = Math.max(2000, Math.min(20000, Math.round(T_as_shot + params.temp * 80)));
                        sliderOnChange = (e) => {
                          const val = parseInt(e.target.value, 10);
                          const offset = (val - T_as_shot) / 80.0;
                          setParams(p => ({ ...p, temp: offset }));
                        };
                        sliderClass = `slider slider-temp${isDirty ? ' slider--dirty' : ''}`;
                      } else if (key === 'tint') {
                        sliderMin = -150;
                        sliderMax = 150;
                        sliderStep = 1;
                        sliderVal = Math.max(-150, Math.min(150, Math.round(tint_as_shot + params.tint * 2)));
                        sliderOnChange = (e) => {
                          const val = parseInt(e.target.value, 10);
                          const offset = (val - tint_as_shot) / 2.0;
                          setParams(p => ({ ...p, tint: offset }));
                        };
                        sliderClass = `slider slider-tint${isDirty ? ' slider--dirty' : ''}`;
                      }

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
                            type="range" min={sliderMin} max={sliderMax} step={sliderStep}
                            value={sliderVal} onChange={sliderOnChange}
                            className={sliderClass}
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
