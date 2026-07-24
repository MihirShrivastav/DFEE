import { useRef, useCallback } from 'react';

// A draggable colour wheel: angle = hue (0=red/right, 90=yellow/up, matching the OKLab
// a/b grading convention), radius = saturation. Double-click to reset to neutral.
function ColorWheel({ label, hue, sat, onChange, size = 108 }) {
  const ref = useRef(null);

  const pick = useCallback((e) => {
    const rect = ref.current.getBoundingClientRect();
    const dx = e.clientX - (rect.left + rect.width / 2);
    const dy = e.clientY - (rect.top + rect.height / 2);
    let ang = (Math.atan2(-dy, dx) * 180) / Math.PI;
    if (ang < 0) ang += 360;
    const rad = Math.min(Math.hypot(dx, dy) / (rect.width / 2), 1);
    onChange({ hue: Math.round(ang), sat: Math.round(rad * 100) });
  }, [onChange]);

  const onDown = (e) => {
    e.preventDefault();
    e.currentTarget.setPointerCapture(e.pointerId);
    pick(e);
  };
  const onMove = (e) => { if (e.buttons === 1) pick(e); };

  const hx = 50 + Math.cos((hue * Math.PI) / 180) * (sat / 100) * 50;
  const hy = 50 - Math.sin((hue * Math.PI) / 180) * (sat / 100) * 50;
  const swatch = sat > 0 ? `hsl(${hue}, ${Math.min(sat, 100)}%, 55%)` : 'var(--bg-elevated)';

  return (
    <div className="cg-wheel-wrap">
      <div
        ref={ref}
        className="cg-wheel"
        style={{ width: size, height: size }}
        onPointerDown={onDown}
        onPointerMove={onMove}
        onDoubleClick={() => onChange({ hue: 0, sat: 0 })}
        title={`${label} — drag to grade, double-click to reset`}
      >
        <div className="cg-wheel-handle" style={{ left: `${hx}%`, top: `${hy}%`, background: swatch }} />
      </div>
      <span className="cg-wheel-label">{label}</span>
    </div>
  );
}

export default function ColorGradingPanel({ cg, onChange }) {
  const patch = (obj) => onChange({ ...cg, ...obj });

  const zone = (z, label) => (
    <div className="cg-zone" key={z}>
      <ColorWheel
        label={label}
        hue={cg[`${z}_hue`]}
        sat={cg[`${z}_sat`]}
        onChange={({ hue, sat }) => patch({ [`${z}_hue`]: hue, [`${z}_sat`]: sat })}
      />
      <div className="slider-row cg-lum-row">
        <div className="slider-header">
          <span className="slider-label">Luminance</span>
          <div className="slider-controls">
            {cg[`${z}_lum`] !== 0 && (
              <button className="revert-btn" title="Reset" onClick={() => patch({ [`${z}_lum`]: 0 })}>Reset</button>
            )}
            <span className={`slider-value${cg[`${z}_lum`] !== 0 ? ' slider-value--dirty' : ''}`}>
              {(cg[`${z}_lum`] > 0 ? '+' : '') + cg[`${z}_lum`]}
            </span>
          </div>
        </div>
        <input
          type="range" min={-100} max={100} step={1} value={cg[`${z}_lum`]}
          onChange={(e) => patch({ [`${z}_lum`]: Number(e.target.value) })}
          className={`slider${cg[`${z}_lum`] !== 0 ? ' slider--dirty' : ''}`}
        />
      </div>
    </div>
  );

  const globalSlider = (key, label, min, max, tooltip) => {
    const dirty = cg[key] !== 0;
    return (
      <div className="slider-row" key={key}>
        <div className="slider-header">
          <span className="slider-label" title={tooltip}>{label}</span>
          <div className="slider-controls">
            {dirty && <button className="revert-btn" title={`Reset ${label}`} onClick={() => patch({ [key]: 0 })}>Reset</button>}
            <span className={`slider-value${dirty ? ' slider-value--dirty' : ''}`}>
              {(cg[key] > 0 ? '+' : '') + cg[key]}
            </span>
          </div>
        </div>
        <input
          type="range" min={min} max={max} step={1} value={cg[key]}
          onChange={(e) => patch({ [key]: Number(e.target.value) })}
          className={`slider${dirty ? ' slider--dirty' : ''}`}
        />
      </div>
    );
  };

  return (
    <div className="cg-panel">
      <div className="cg-wheels">
        {zone('shadow', 'Shadows')}
        {zone('midtone', 'Midtones')}
        {zone('highlight', 'Highlights')}
        {zone('global', 'Global')}
      </div>
      <div className="material-subhead">Grade</div>
      {globalSlider('crossbalance', 'Film Crossbalance', -100, 100, 'One-knob split-tone: forward for teal shadows + warm highlights, back for the inverse.')}
      {globalSlider('balance', 'Balance', -100, 100, 'Shifts where shadows end and highlights begin.')}
      {globalSlider('blending', 'Blending', 0, 100, 'How softly the shadow/midtone/highlight zones overlap.')}
    </div>
  );
}
