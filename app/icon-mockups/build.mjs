// Generates the icon concept SVG masters and the comparison page.
// Run: node app/icon-mockups/build.mjs
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));

const BLUE = '#5aa9ff';
const IR = '#ff3d6e';
const IR_DEEP = '#c3124f';

// Shared defs. The macOS plate is 824x824 at (100,100) with a 22.5% corner radius.
const commonDefs = `
    <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#1d2331"/>
      <stop offset="1" stop-color="#0e1014"/>
    </linearGradient>
    <filter id="shadow" x="-10%" y="-10%" width="120%" height="125%">
      <feDropShadow dx="0" dy="12" stdDeviation="14" flood-color="#000" flood-opacity=".35"/>
    </filter>
    <filter id="glow" x="-60%" y="-60%" width="220%" height="220%">
      <feGaussianBlur stdDeviation="26"/>
    </filter>`;

const concepts = [
  {
    id: '01-sensor-eye',
    name: 'Sensor eye',
    line: 'An eye whose pupil glows infrared: the device sees light you cannot.',
    linux: 'Works full-bleed. The centred shape scales up cleanly.',
    defs: `
    <radialGradient id="pupil" cx=".42" cy=".38" r=".7">
      <stop offset="0" stop-color="#ffe0e8"/>
      <stop offset=".35" stop-color="${IR}"/>
      <stop offset="1" stop-color="${IR_DEEP}"/>
    </radialGradient>`,
    art: `
  <path d="M206 512 C320 290 704 290 818 512 C704 734 320 734 206 512 Z" fill="#0a0c10" stroke="${BLUE}" stroke-width="60" stroke-linejoin="round"/>
  <circle cx="512" cy="512" r="128" fill="${IR}" opacity=".85" filter="url(#glow)"/>
  <circle cx="512" cy="512" r="108" fill="url(#pupil)"/>
  <circle cx="474" cy="472" r="24" fill="#fff" opacity=".85"/>`,
  },
  {
    id: '02-pulse-train',
    name: 'Pulse train',
    line: 'Square pulses like the app’s graph, with the second pulse lit in infrared.',
    linux: 'Works full-bleed. The wide waveform fills a square well.',
    art: `
  <path d="M584 656 V368 H724 V656" fill="none" stroke="${IR}" stroke-width="104" stroke-linejoin="round" stroke-linecap="round" opacity=".9" filter="url(#glow)"/>
  <path d="M196 656 H300 V368 H440 V656 H584" fill="none" stroke="${BLUE}" stroke-width="80" stroke-linejoin="round" stroke-linecap="round"/>
  <path d="M724 656 H828" fill="none" stroke="${BLUE}" stroke-width="80" stroke-linecap="round"/>
  <path d="M584 656 V368 H724 V656" fill="none" stroke="${IR}" stroke-width="80" stroke-linejoin="round" stroke-linecap="round"/>`,
  },
  {
    id: '03-pulse-timer',
    name: 'Pulse timer',
    line: 'A stopwatch ring around one infrared pulse: timing light.',
    linux: 'Works full-bleed, though the crown gets close to the top edge.',
    art: `
  <rect x="470" y="196" width="84" height="76" rx="16" fill="${BLUE}"/>
  <circle cx="512" cy="548" r="262" fill="#0a0c10" stroke="${BLUE}" stroke-width="56"/>
  <path d="M348 610 H440 V470 H584 V610 H676" fill="none" stroke="${IR}" stroke-width="74" stroke-linejoin="round" stroke-linecap="round" opacity=".9" filter="url(#glow)"/>
  <path d="M348 610 H440 V470 H584 V610 H676" fill="none" stroke="${IR}" stroke-width="56" stroke-linejoin="round" stroke-linecap="round"/>`,
  },
  {
    id: '04-beam-monogram',
    name: 'Beam monogram',
    line: 'A blue R with a glowing infrared bar beside it, so the pair reads “IR”.',
    linux: 'Works full-bleed. Letterforms hold up at any crop.',
    defs: `
    <linearGradient id="beam-i" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ff8aa6"/>
      <stop offset="1" stop-color="${IR_DEEP}"/>
    </linearGradient>`,
    art: `
  <rect x="270" y="290" width="96" height="444" rx="20" fill="${IR}" opacity=".85" filter="url(#glow)"/>
  <rect x="270" y="290" width="96" height="444" rx="20" fill="url(#beam-i)"/>
  <path d="M470 734 V338 H600 A108 108 0 0 1 600 554 H470 M582 554 L716 734" fill="none" stroke="${BLUE}" stroke-width="96" stroke-linejoin="round" stroke-linecap="butt"/>`,
  },
  {
    id: '05-beam-detector',
    name: 'Beam to detector',
    line: 'An infrared beam spreading from a source and landing on a blue detector.',
    linux: 'Works full-bleed. The horizontal layout leaves some empty space top and bottom.',
    defs: `
    <linearGradient id="beam" x1="292" y1="0" x2="640" y2="0" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="${IR}" stop-opacity=".95"/>
      <stop offset="1" stop-color="${IR}" stop-opacity=".45"/>
    </linearGradient>`,
    art: `
  <path d="M692 694 V786 M766 694 V786" stroke="#3b4a63" stroke-width="26" stroke-linecap="round"/>
  <path d="M262 482 L640 396 V628 L262 542 Z" fill="url(#beam)"/>
  <circle cx="262" cy="512" r="70" fill="${IR}" filter="url(#glow)"/>
  <circle cx="262" cy="512" r="50" fill="#ffd6e0"/>
  <rect x="640" y="330" width="172" height="364" rx="40" fill="${BLUE}"/>
  <rect x="628" y="396" width="44" height="232" rx="22" fill="${IR}" opacity=".9" filter="url(#glow)"/>
  <rect x="640" y="396" width="34" height="232" rx="17" fill="#ffd6e0"/>`,
  },
];

function svg(c, shape) {
  const plate =
    shape === 'mac'
      ? `<rect x="100" y="100" width="824" height="824" rx="185" fill="url(#plate)" filter="url(#shadow)"/>
  <rect x="102" y="102" width="820" height="820" rx="183" fill="none" stroke="#fff" stroke-opacity=".09" stroke-width="4"/>`
      : `<rect width="1024" height="1024" fill="url(#plate)"/>`;
  const art =
    shape === 'mac' ? c.art : `<g transform="translate(512 512) scale(1.16) translate(-512 -512)">${c.art}\n  </g>`;
  return `<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">
  <defs>${commonDefs}${c.defs ?? ''}
  </defs>
  ${plate}${art}
</svg>
`;
}

for (const c of concepts) {
  writeFileSync(join(here, `${c.id}.svg`), svg(c, 'mac'));
  writeFileSync(join(here, `${c.id}-square.svg`), svg(c, 'square'));
}

const data = concepts.map((c, i) => ({
  n: i + 1,
  id: c.id,
  name: c.name,
  line: c.line,
  linux: c.linux,
  mac: svg(c, 'mac'),
  square: svg(c, 'square'),
}));

const template = (await import('node:fs')).readFileSync(join(here, 'page.template.html'), 'utf8');
writeFileSync(join(here, 'index.html'), template.replace('/*CONCEPTS*/[]', JSON.stringify(data)));
console.log(`wrote ${concepts.length * 2} SVGs and index.html`);
