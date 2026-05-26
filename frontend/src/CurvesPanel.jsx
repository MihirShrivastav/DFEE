import React, { useRef, useCallback, useEffect } from 'react';

const W = 200;   // SVG canvas size
const H = 200;
const R = 3.5;   // visible dot radius (smaller = less clutter)
const HIT = 8;   // invisible hit-area radius for easier grabbing

/** Map a curve [x,y] (0–1) to SVG coords */
const toSvg = ([x, y]) => [x * W, (1 - y) * H];

/** Map raw client coords back to curve [x,y] using SVG bounding rect */
const fromSvg = (clientX, clientY, rect) => {
  const x = (clientX - rect.left)  / rect.width;
  const y = 1 - (clientY - rect.top) / rect.height;
  return [Math.max(0, Math.min(1, x)), Math.max(0, Math.min(1, y))];
};

/** Catmull-Rom → SVG cubic bezier for a smooth preview curve */
function buildPath(sorted) {
  if (sorted.length < 2) return '';
  const sp = sorted.map(toSvg);
  if (sorted.length === 2) {
    return `M${sp[0][0]},${sp[0][1]} L${sp[1][0]},${sp[1][1]}`;
  }
  let d = `M${sp[0][0]},${sp[0][1]}`;
  for (let i = 0; i < sp.length - 1; i++) {
    const p0 = sp[Math.max(i - 1, 0)];
    const p1 = sp[i];
    const p2 = sp[i + 1];
    const p3 = sp[Math.min(i + 2, sp.length - 1)];
    const cp1x = p1[0] + (p2[0] - p0[0]) / 6;
    const cp1y = p1[1] + (p2[1] - p0[1]) / 6;
    const cp2x = p2[0] - (p3[0] - p1[0]) / 6;
    const cp2y = p2[1] - (p3[1] - p1[1]) / 6;
    d += ` C${cp1x},${cp1y} ${cp2x},${cp2y} ${p2[0]},${p2[1]}`;
  }
  return d;
}

const DEFAULT_POINTS = [[0, 0], [0.25, 0.25], [0.5, 0.5], [0.75, 0.75], [1, 1]];

export default function CurvesPanel({ points, onChange }) {
  const svgRef    = useRef(null);
  const dragRef   = useRef(null);    // { origX, origY } — position we are dragging
  /**
   * KEY FIX: pointsRef always holds the latest `points` value.
   * Without this, onMove closures capture a stale snapshot of `points`
   * from the time of mousedown, so after the first onChange the dragged
   * point can no longer be found by coordinate lookup — it appears frozen.
   */
  const pointsRef = useRef(points);
  useEffect(() => { pointsRef.current = points; }, [points]);

  const sorted = [...points].sort((a, b) => a[0] - b[0]);
  const pathD  = buildPath(sorted);

  /* ── Add a point by clicking empty canvas ────────────────────────────── */
  const handleSvgClick = useCallback((e) => {
    if (dragRef.current) return;         // ignore mouseup-then-click after drag
    if (!svgRef.current) return;
    const [x, y] = fromSvg(e.clientX, e.clientY, svgRef.current.getBoundingClientRect());
    // Don't add a point on top of an existing one
    const tooClose = pointsRef.current.some(
      p => Math.hypot(p[0] - x, p[1] - y) < 0.05
    );
    if (tooClose) return;
    onChange([...pointsRef.current, [x, y]]);
  }, [onChange]);

  /* ── Drag a point ────────────────────────────────────────────────────── */
  const handlePointMouseDown = useCallback((e, ptX, ptY) => {
    e.preventDefault();
    e.stopPropagation();
    dragRef.current = { origX: ptX, origY: ptY };

    const onMove = (me) => {
      if (!svgRef.current || !dragRef.current) return;
      const { origX, origY } = dragRef.current;
      const [nx, ny] = fromSvg(me.clientX, me.clientY,
                                svgRef.current.getBoundingClientRect());
      // ── Use pointsRef.current (NOT stale closure) so we always find the
      //    right point even after multiple consecutive onChange calls. ──────
      const updated = pointsRef.current.map(p =>
        Math.abs(p[0] - origX) < 0.002 && Math.abs(p[1] - origY) < 0.002
          ? [nx, ny] : p
      );
      dragRef.current = { origX: nx, origY: ny };
      onChange(updated);
    };

    const onUp = () => {
      setTimeout(() => { dragRef.current = null; }, 10);
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup',   onUp);
    };

    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup',   onUp);
  }, [onChange]);   // ← no 'points' dependency needed; we use pointsRef

  /* ── Remove a point on double-click (keep ≥ 2 points) ───────────────── */
  const handlePointDblClick = useCallback((e, ptX, ptY) => {
    e.stopPropagation();
    const cur = pointsRef.current;
    if (cur.length <= 2) return;
    onChange(cur.filter(
      p => !(Math.abs(p[0] - ptX) < 0.002 && Math.abs(p[1] - ptY) < 0.002)
    ));
  }, [onChange]);

  /* ── Identity check for showing Reset button ─────────────────────────── */
  const sortedDef = [...DEFAULT_POINTS].sort((a, b) => a[0] - b[0]);
  const isIdentity = sorted.length === sortedDef.length
    && sorted.every((p, i) =>
        Math.abs(p[0] - sortedDef[i][0]) < 0.01 &&
        Math.abs(p[1] - sortedDef[i][1]) < 0.01);

  return (
    <div className="curves-panel">
      <svg
        ref={svgRef}
        className="curves-canvas"
        viewBox={`0 0 ${W} ${H}`}
        onClick={handleSvgClick}
      >
        <rect x={0} y={0} width={W} height={H} className="curves-bg" />

        {/* Grid lines */}
        {[0.25, 0.5, 0.75].map(v => (
          <g key={v}>
            <line x1={v * W} y1={0}  x2={v * W} y2={H}  className="curves-gridline" />
            <line x1={0}     y1={v*H} x2={W}     y2={v*H} className="curves-gridline" />
          </g>
        ))}

        {/* Diagonal reference */}
        <line x1={0} y1={H} x2={W} y2={0} className="curves-diagonal" />

        {/* Curve path */}
        <path d={pathD} className="curves-path" />

        {/* Control points — small visible dot + large invisible hit area */}
        {sorted.map((pt, i) => {
          const [sx, sy] = toSvg(pt);
          return (
            <g key={i}>
              {/* Invisible large hit circle so small dots are still easy to grab */}
              <circle
                cx={sx} cy={sy} r={HIT}
                fill="transparent"
                style={{ cursor: 'grab' }}
                onMouseDown={(e) => handlePointMouseDown(e, pt[0], pt[1])}
                onDoubleClick={(e) => handlePointDblClick(e, pt[0], pt[1])}
              />
              {/* Visible dot — purely decorative, pointer-events disabled */}
              <circle
                cx={sx} cy={sy} r={R}
                className="curves-point"
                style={{ pointerEvents: 'none' }}
              />
            </g>
          );
        })}
      </svg>

      <div className="curves-footer">
        <span className="curves-hint">Click to add · Drag · Dbl-click to remove</span>
        {!isIdentity && (
          <button className="curves-reset" onClick={() => onChange(DEFAULT_POINTS)}>
            Reset
          </button>
        )}
      </div>
    </div>
  );
}

export { DEFAULT_POINTS };
