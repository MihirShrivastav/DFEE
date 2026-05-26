import React, { useState } from 'react';

const HSL_RANGES = [
  { key: 'red',     label: 'Red',     color: '#f25c5c' },
  { key: 'orange',  label: 'Orange',  color: '#f2944a' },
  { key: 'yellow',  label: 'Yellow',  color: '#d4c62a' },
  { key: 'green',   label: 'Green',   color: '#4db858' },
  { key: 'aqua',    label: 'Aqua',    color: '#38c0c0' },
  { key: 'blue',    label: 'Blue',    color: '#4a85e8' },
  { key: 'purple',  label: 'Purple',  color: '#9b5de5' },
  { key: 'magenta', label: 'Magenta', color: '#d44fa8' },
];

const TABS = ['H', 'S', 'L'];
const TAB_LABELS = { H: 'Hue', S: 'Saturation', L: 'Luminance' };

export default function HslPanel({ hsl, onChange }) {
  const [activeTab, setActiveTab] = useState('H');

  const suffix = activeTab.toLowerCase();     // 'h', 's', or 'l'

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
            {t}
          </button>
        ))}
        {isDirty && (
          <button
            className="hsl-reset"
            onClick={() => onChange(Object.fromEntries(
              HSL_RANGES.flatMap(r => ['h', 's', 'l'].map(p => [`${r.key}_${p}`, 0]))
            ))}
          >
            Reset all
          </button>
        )}
      </div>

      {/* Slider rows */}
      <div className="hsl-sliders">
        {HSL_RANGES.map(({ key, label, color }) => {
          const paramKey = `${key}_${suffix}`;
          const val      = hsl[paramKey] ?? 0;
          const dirty    = val !== 0;
          return (
            <div className="hsl-row" key={key}>
              {/* Colour swatch */}
              <div className="hsl-swatch" style={{ background: color }} />

              {/* Label */}
              <span className="hsl-label">{label}</span>

              {/* Slider */}
              <input
                type="range"
                className="hsl-slider"
                min={activeTab === 'H' ? -180 : -100}
                max={activeTab === 'H' ? 180  :  100}
                step={1}
                value={val}
                onChange={e => handleSlider(key, e.target.value)}
              />

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
