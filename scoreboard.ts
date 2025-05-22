import axios from 'axios';
import * as cheerio from 'cheerio';

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

let contest_id = "y9s9zPhwJPE";
let task_id = "7090546";
let page = "https://course.educg.net//pages/contest/contest_rank_more.jsp";
let url = `${page}?contestID=${contest_id}&taskID=${task_id}`;

// Note that it is not quite possible to fetch the result.
// I need to somehow login first.
fetch(url);
