import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

const files = ['main/plank_ui.c', 'main/health_ui.c', 'main/health_portal.c'];
const extra = '我刚喝了一杯水卧推公斤个休息两分钟久坐提醒起来走走记录今天累计深蹲硬拉撤销跟练';
const source = files.map((file) => readFileSync(file, 'utf8')).join('') + extra;
const symbols = [...new Set([...source].filter(c => /\p{Script=Han}/u.test(c)))].join('');
const result = spawnSync('npx', ['--yes', 'lv_font_conv@1.5.3', '--font',
  'assets/fonts/SourceHanSansCN-Normal.otf', '--symbols', symbols,
  '-r', '0x20-0x7e,0x3001,0x3002,0xFF0C,0xFF1A,0x2026', '--size', '16', '--bpp', '4', '--format', 'lvgl',
  '--no-compress', '--no-kerning', '--lv-include', 'lvgl.h', '--lv-font-name', 'plank_zh_16',
  '-o', 'assets/fonts/plank_zh_16.c'], { stdio: 'inherit' });
process.exit(result.status ?? 1);
