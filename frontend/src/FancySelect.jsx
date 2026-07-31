import { useState, useRef, useEffect } from 'react';

/**
 * Graphite dropdown — a styled trigger + popover list with optional swatch and
 * per-option subtitle. Drop-in replacement for a grouped native <select>.
 *
 *   groups: [{ label, options: [{ id, name, sub, type }] }]
 *   swatchSrc(option) -> image URL, or null for a neutral placeholder (optional;
 *                        omit entirely for text-only lists)
 */
export default function FancySelect({ value, groups, onChange, placeholder = 'Select…', swatchSrc }) {
  const [open, setOpen] = useState(false);
  const ref = useRef(null);

  useEffect(() => {
    if (!open) return undefined;
    const onDoc = (e) => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
    const onKey = (e) => { if (e.key === 'Escape') setOpen(false); };
    document.addEventListener('mousedown', onDoc);
    document.addEventListener('keydown', onKey);
    return () => { document.removeEventListener('mousedown', onDoc); document.removeEventListener('keydown', onKey); };
  }, [open]);

  const all = groups.flatMap((g) => g.options);
  const sel = all.find((o) => o.id === value);
  const hasSwatch = typeof swatchSrc === 'function';

  const Swatch = ({ opt, sm }) => {
    const cls = `fsel-swatch${sm ? ' fsel-swatch--sm' : ''}`;
    const url = opt ? swatchSrc(opt) : null;
    return url
      ? <img className={cls} src={url} alt="" />
      : <span className={`${cls} fsel-swatch--empty`} />;
  };

  return (
    <div className={`fsel${open ? ' open' : ''}${hasSwatch ? ' fsel--rich' : ''}`} ref={ref}>
      <button type="button" className="fsel-trigger" onClick={() => setOpen((o) => !o)}>
        {hasSwatch && <Swatch opt={sel} />}
        <span className="fsel-meta">
          <span className="fsel-name">{sel ? sel.name : placeholder}</span>
          {sel && sel.sub && <span className="fsel-sub">{sel.sub}</span>}
        </span>
        <span className="fsel-chev">⌄</span>
      </button>

      {open && (
        <div className="fsel-menu" role="listbox">
          {groups.map((g, gi) => (
            <div className="fsel-group" key={g.label || `g${gi}`}>
              {g.label && <div className="fsel-group-label">{g.label}</div>}
              {g.options.map((o) => (
                <button
                  type="button"
                  key={o.id}
                  role="option"
                  aria-selected={o.id === value}
                  className={`fsel-opt${o.id === value ? ' on' : ''}`}
                  onClick={() => { onChange(o.id); setOpen(false); }}
                >
                  {hasSwatch && <Swatch opt={o} sm />}
                  <span className="fsel-meta">
                    <span className="fsel-name">{o.name}</span>
                    {o.sub && <span className="fsel-sub">{o.sub}</span>}
                  </span>
                  {o.id === value && <span className="fsel-check">✓</span>}
                </button>
              ))}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
