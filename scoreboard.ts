#!pnpm tsx
import axios from 'axios';
import * as cheerio from 'cheerio';
import * as fs from 'fs';
import * as readline from 'readline';

interface ScoreEntry {
  id: string;
  name: string;
  status: string;
  time: number;
  best: number;
  team: string;
}

let contest_id = "y9s9zPhwJPE";
let task_id = "7090546";
let page = "https://course.educg.net//pages/contest/contest_rank_more.jsp";
let url = `${page}?contestID=${contest_id}&taskID=${task_id}`;

async function fetch(url: string) {

  // Fetch the content.
  const response = await axios.get(url);
  const html: string = response.data;
  const $: cheerio.CheerioAPI = cheerio.load(html);
  console.log("Fetched url:\n");
  console.log(html);

  const rows = $("table tbody tr");
  const result: [string, number, number][] = [];
  rows.each((index: number, row) => {
    const cells = $(row).find("td");
    const name: string = $(cells.get(0)).text();
    const self_time = parseFloat($(cells.get(2)).text());
    const best_time = parseFloat($(cells.get(3)).text());
    result.push([ name, self_time, best_time ]);
  });

  for (let [name, self, best] of result)
    console.log(`${name},${self},${best}`);

}

function parseLine(line: string): ScoreEntry | null {
  const parts = line.trim().split(/\s+/);
  if (parts.length < 6) return null;

  return {
    id: parts[0],
    name: parts[1],
    status: parts[2],
    time: parseFloat(parts[3]),
    best: parseFloat(parts[4]),
    team: parts.slice(5).join(""),
  };
}

async function read(filePath: string): Promise<ScoreEntry[]> {
  const fileStream = fs.createReadStream(filePath);
  const rl = readline.createInterface({
    input: fileStream,
    crlfDelay: Infinity,
  });

  const entries: ScoreEntry[] = [];
  for await (const line of rl) {
    const parsed = parseLine(line);
    if (parsed) {
      entries.push(parsed);
    }
  }

  return entries;
}

function compare(f1: ScoreEntry[], f2: ScoreEntry[], threshold = 0.1) {
  console.log("Significant changes:");
  f1.forEach((a, i) => {
    const b = f2[i];
    const delta = b.time - a.time;
    if (Math.abs(delta) >= threshold) {
      const plus = delta > 0 ? "+" : "";
      const change = `${a.time.toFixed(2)} -> ${b.time.toFixed(2)}`.padEnd(16);
      console.log(
        `${a.name.padEnd(15)} ${change} (${plus}${delta.toFixed(2)})`
      );
    }
  });
}

async function main() {
  // The first two arguments are node-path and script path.
  if (process.argv.length != 3) {
    console.log("usage: scoreboard.ts <file count>");
    return;
  }

  const count = parseInt(process.argv[2]);

  const file1 = `rank/${count}.txt`;
  const file2 = `rank/${count + 1}.txt`;

  const data1 = await read(file1);
  const data2 = await read(file2);

  if (data1.length !== data2.length) {
    console.error(`different entry count: ${data1.length} != ${data2.length}`);
    return;
  }

  compare(data1, data2);
}

main().catch(err => console.error(err));
