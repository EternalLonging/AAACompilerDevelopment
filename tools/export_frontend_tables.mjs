/** 将前端表格数据导出为中文 Excel，并生成本地校验预览。 */
import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const args = process.argv.slice(2);
if (args.length !== 4) {
  throw new Error('用法：node tools/export_frontend_tables.mjs 数据.json 输出.xlsx 预览目录 node_modules目录');
}
const [dataPath, outputPath, previewDir, modulesDir] = args.map(value => path.resolve(value));
// 使用外部提供的 artifact-tool，不向仓库安装或提交依赖。
const require = createRequire(path.join(modulesDir, '..', 'artifact-loader.cjs'));
const { Workbook, SpreadsheetFile } = await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const data = JSON.parse(await fs.readFile(dataPath, 'utf8'));
const workbook = Workbook.create();
const font = 'Arial';

function columnName(index) {
  let text = '';
  while (index > 0) {
    index--;
    text = String.fromCharCode(65 + index % 26) + text;
    index = Math.floor(index / 26);
  }
  return text;
}

for (const [sheetIndex, spec] of data.sheets.entries()) {
  const sheet = workbook.worksheets.add(spec.name);
  sheet.showGridLines = false;
  const last = columnName(spec.headers.length);
  const end = data.header_row + spec.rows.length;
  const range = sheet.getRange(`A2:${last}${end}`);
  range.format.font = { name: font, size: 11, color: '#172B42' };
  range.format.rowHeight = 24;
  range.format.verticalAlignment = 'center';
  range.format.wrapText = false;
  sheet.getRange('A2').values = [[spec.title]];
  sheet.getRange('A2').format.font = { name: font, size: 16, bold: true, color: '#172B42' };
  sheet.getRange('A2').format.rowHeight = 32;
  sheet.getRange('A3').values = [[spec.context]];
  sheet.getRange('A3').format.font = { name: font, size: 11, color: '#526477' };
  spec.notes.forEach((note, index) => {
    sheet.getRange(`A${index + 4}`).values = [[note]];
  });
  const literal = value => typeof value === 'string' && value.startsWith('=') ? "'" + value : value;
  const values = [spec.headers, ...spec.rows].map(row => row.map(literal));
  sheet.getRange(`A${data.header_row}:${last}${end}`).values = values;
  const heading = sheet.getRange(`A${data.header_row}:${last}${data.header_row}`);
  heading.format.fill = '#243C58';
  heading.format.font = { name: font, size: 11, bold: true, color: '#FFFFFF' };
  heading.format.horizontalAlignment = 'center';
  heading.format.rowHeight = 30;
  heading.format.borders = { insideVertical: { style: 'thin', color: '#FFFFFF' } };
  spec.widths.forEach((width, index) => {
    const col = columnName(index + 1);
    sheet.getRange(`${col}2:${col}${end}`).format.columnWidth = width;
  });
  // 顶部说明跨列换行，窄表也能完整显示。
  [spec.context, ...spec.notes].forEach((text, index) => {
    const row = index + 3;
    const noteColumns = Math.min(spec.headers.length, 10);
    const note = sheet.getRange(`A${row}:${columnName(noteColumns)}${row}`);
    note.merge();
    note.format.wrapText = true;
    const length = [...text].reduce((sum, char) => sum + (char.charCodeAt(0) > 255 ? 2 : 1), 0);
    const width = spec.widths.slice(0, noteColumns).reduce((sum, value) => sum + value, 0) - 8;
    note.format.rowHeight = 24 * Math.max(1, Math.ceil(length / width));
  });
  // 表头下方按行浅色分隔，方便沿宽矩阵横向阅读。
  for (let row = data.header_row + 2; row <= end; row += 2) {
    sheet.getRange(`A${row}:${last}${row}`).format.fill = '#F0F4F8';
  }
  const table = sheet.tables.add(`A${data.header_row}:${last}${end}`, true,
                                `FrontendTable${sheetIndex + 1}`);
  table.style = 'TableStyleMedium2';
  table.showFilterButton = true;
  sheet.freezePanes.freezeRows(data.header_row);
  sheet.freezePanes.freezeColumns(spec.name === '状态转换矩阵' ? 2 : 1);
  if (spec.name === '状态转换矩阵') {
    sheet.tabColor = '#243C58';
    sheet.getRange(`A8:${last}${end}`).format.numberFormat = '0';
    sheet.getRange(`C8:${last}${end}`).conditionalFormats.add('cellIs', {
      operator: 'equal', formula: -1, format: { font: { color: '#8794A3' } },
    });
    sheet.getRange(`A8:B${end}`).format.fill = '#E3EBF3';
  }
  // 明确数值字段的格式与对齐，编号和矩阵值不转换为字符串。
  for (let index = 0; index < spec.headers.length; index++) {
    const col = columnName(index + 1);
    const numeric = spec.rows.every(row => typeof row[index] === 'number');
    sheet.getRange(`${col}8:${col}${end}`).format.horizontalAlignment = numeric ? 'right' : 'left';
    sheet.getRange(`${col}8:${col}${end}`).format.numberFormat = numeric ? '0' : '@';
  }
  // 只为需要换行的长规则和说明增加行高，保持其他表格紧凑。
  spec.rows.forEach((values, index) => {
    let lines = 1;
    values.forEach((value, col) => {
      if (typeof value !== 'string') return;
      const width = [...value].reduce((sum, char) => sum + (char.charCodeAt(0) > 255 ? 2 : 1), 0);
      const needed = Math.ceil(width / Math.max(spec.widths[col] - 2, 1));
      if (needed > 1) {
        sheet.getRange(`${columnName(col + 1)}${index + 8}`).format.wrapText = true;
        lines = Math.max(lines, needed);
      }
    });
    if (lines > 1) sheet.getRange(`A${index + 8}:${last}${index + 8}`).format.rowHeight = 24 * lines;
  });
}

workbook.recalculate();
await fs.mkdir(previewDir, { recursive: true });
const firstSheet = data.sheets[0];
console.log((await workbook.inspect({ kind: 'region', sheetId: firstSheet.name,
  range: `A7:${columnName(Math.min(firstSheet.headers.length, 10))}10`,
  maxChars: 2200, tableMaxRows: 4, tableMaxCols: 10 })).ndjson);
console.log((await workbook.inspect({ kind: 'match',
  searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',
  options: { useRegex: true, maxResults: 20 }, maxChars: 1200 })).ndjson);

for (const spec of data.sheets) {
  const last = columnName(Math.min(spec.headers.length, 10));
  const preview = await workbook.render({ sheetName: spec.name, range: `A1:${last}15`, scale: 1.4,
                                          format: 'png' });
  await fs.writeFile(path.join(previewDir, `${spec.name}.png`), new Uint8Array(await preview.arrayBuffer()));
}
await fs.mkdir(path.dirname(outputPath), { recursive: true });
await (await SpreadsheetFile.exportXlsx(workbook)).save(outputPath);
// 导出器生成的检查日志属于过程文件，集中存放在预览目录。
const inspectPath = outputPath + '.inspect.ndjson';
try {
  await fs.rename(inspectPath, path.join(previewDir, '导出检查.ndjson'));
} catch (error) {
  if (error.code !== 'ENOENT') throw error;
}
console.log(`已导出：${outputPath}`);
