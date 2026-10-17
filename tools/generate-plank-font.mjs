import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

const source = readFileSync('main/plank_ui.c', 'utf8');
const symbols = [...new Set([...source].filter(c => /\p{Script=Han}/u.test(c)))].join('');
const result = spawnSync('npx', ['--yes', 'lv_font_conv@1.5.3', '--font',
  'assets/fonts/SourceHanSansCN-Normal.otf', '--symbols', symbols,
  '-r', '0x20-0x7e', '--size', '16', '--bpp', '4', '--format', 'lvgl',
  '--no-compress', '--no-kerning', '--lv-include', 'lvgl.h', '--lv-font-name', 'plank_zh_16',
  '-o', 'assets/fonts/plank_zh_16.c'], { stdio: 'inherit' });
process.exit(result.status ?? 1);
