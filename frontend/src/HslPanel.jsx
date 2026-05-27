import React, { useState, useMemo } from 'react';

// ─── Color definitions ────────────────────────────────────────────────────────
// hue: CSS hue degree (0-360), sat/lum in % for the swatch
const HSL_RANGES = [
  { key: 'red',     label: 'Red',     hue:   0, sat: 85, lum: 55, color: '#f25c5c' },
  { key: 'orange',  label: 'Orange',  hue:  28, sat: 88, lum: 60, color: '#f2944a' },
  { key: 'yellow',  label: 'Yellow',  hue:  54, sat: 80, lum: 50, color: '#d4c62a' },
  { key: 'green',   label: 'Green',   hue: 122, sat: 45, lum: 48, color: '#4db858' },
  { key: 'aqua',    label: 'Aqua',    hue: 180, sat: 55, lum: 48, color: '#38c0c0' },
  { key: 'blue',    label: 'Blue',    hue: 218, sat: 75, lum: 58, color: '#4a85e8' },
  { key: 'purple',  label: 'Purple',  hue: 272, sat: 72, lum: 58, color: '#9b5de5' },
  { key: 'magenta', label: 'Magenta', hue: 315, sat: 62, lum: 55, color: '#d44fa8' },
];

const TABS = ['H', 'S', 'L'];

// ─── Gradient generators ──────────────────────────────────────────────────────
// Each returns a CSS linear-gradient string for the slider track background.

/**
 * HUE gradient: shows the surrounding hue spectrum, ±65° from center hue.
 * The center of the bar = the primary hue of this range (i.e. where val=0 has no effect).
 * Moving left/right on the slider shifts hues; the gradient communicates which source
 * hues are being targeted.
 */
function hueGradient(centerHue, steps = 14) {
  const span = 65; // degrees either side
  const stops = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;                        // 0..1
    const h = ((centerHue - span + t * span * 2) + 360) % 360;
    stops.push(`hsl(${h}, 80%, 55%) ${(t * 100).toFixed(1)}%`);
  }
  return `linear-gradient(to right, ${stops.join(', ')})`;
}

/**
 * SATURATION gradient: left = gray (0% sat), right = full color (100% sat).
 * The hue and luminance stay constant at this range's values.
 */
function satGradient(hue, lum, steps = 8) {
  const stops = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const s = Math.round(t * 100);
    stops.push(`hsl(${hue}, ${s}%, ${lum}%) ${(t * 100).toFixed(1)}%`);
  }
  return `linear-gradient(to right, ${stops.join(', ')})`;
}

/**
 * LUMINANCE gradient: black → pure color → white.
 * Three-stop gradient; the center represents the natural lum of the range.
 */
function lumGradient(hue, sat) {
  return `linear-gradient(to right,
    hsl(${hue}, ${sat}%, 8%),
    hsl(${hue}, ${sat}%, 50%),
    hsl(${hue}, ${sat}%, 92%))`;
}

// ─── Per-tab thumb colour ─────────────────────────────────────────────────────
// When the slider is dirty, tint the thumb with the range's color.

export default function HslPanel({ hsl, onChange }) {
  const [activeTab, setActiveTab] = useState('H');
  const suffix = activeTab.toLowerCase();

  const handleSlider = (rangeName, value) => {
    onChange({ ...hsl, [`${rangeName}_${suffix}`]: Number(value) });
  };

  const isDirty = Object.values(hsl).some(v => v !== 0);

  return (
    <div className="hsl-panel">
      {/* Tab switcher */}
      <div className="hsl-tabs">
        {TABS.map(t => (
          <button
            key={t}
            className={`hsl-tab ${activeTab === t ? 'active' : ''}`}
            onClick={() => setActiveTab(t)}
          >
            {t === 'H' ? 'Hue' : t === 'S' ? 'Saturation' : 'Luminance'}
          </button>
        ))}
        {isDirty && (
          <button
            className="hsl-reset"
            onClick={() => onChange(Object.fromEntries(
              HSL_RANGES.flatMap(r => ['h', 's', 'l'].map(p => [`${r.key}_${p}`, 0]))
            ))}
          >↺</button>
        )}
      </div>

      {/* Slider rows */}
      <div className="hsl-sliders">
        {HSL_RANGES.map(({ key, label, hue, sat, lum, color }) => {
          const paramKey = `${key}_${suffix}`;
          const val      = hsl[paramKey] ?? 0;
          const dirty    = val !== 0;

          // Compute gradient for this tab
          let trackGrad;
          if (activeTab === 'H') trackGrad = hueGradient(hue);
          else if (activeTab === 'S') trackGrad = satGradient(hue, lum);
          else trackGrad = lumGradient(hue, sat);

          // Compute thumb color: match the resulting color based on slider position
          let thumbColor;
          if (activeTab === 'H') {
            const resultHue = ((hue + val) + 360) % 360;
            thumbColor = `hsl(${resultHue}, ${sat}%, ${lum}%)`;
          } else if (activeTab === 'S') {
            const resultSat = Math.max(0, Math.min(100, sat + val));
            thumbColor = `hsl(${hue}, ${resultSat}%, ${lum}%)`;
          } else {
            const resultLum = Math.max(5, Math.min(95, lum + val * 0.35));
            thumbColor = `hsl(${hue}, ${sat}%, ${resultLum}%)`;
          }

          const min = activeTab === 'H' ? -180 : -100;
          const max = activeTab === 'H' ?  180 :  100;

          return (
            <div className="hsl-row" key={key}>
              {/* Colour swatch */}
              <div className="hsl-swatch" style={{ background: color }} />

              {/* Label */}
              <span className="hsl-label">{label}</span>

              {/* Gradient track + slider */}
              <div className="hsl-track-wrap">
                {/* Gradient bar behind the thumb */}
                <div
                  className="hsl-track-gradient"
                  style={{ background: trackGrad }}
                />
                {/* Center tick mark */}
                <div className="hsl-track-center" />
                {/* Thumb overlay */}
                <input
                  type="range"
                  className="hsl-slider"
                  style={{ '--thumb-color': thumbColor }}
                  min={min}
                  max={max}
                  step={1}
                  value={val}
                  onChange={e => handleSlider(key, e.target.value)}
                />
              </div>

              {/* Value */}
              <span className={`hsl-value${dirty ? ' dirty' : ''}`}>
                {val > 0 ? '+' : ''}{val}
              </span>

              {/* Revert */}
              {dirty && (
                <button
                  className="hsl-revert"
                  onClick={() => onChange({ ...hsl, [paramKey]: 0 })}
                >↺</button>
              )}
            </div>
          );
        })}
      </div>
    </div>
  );
}

export { HSL_RANGES };
