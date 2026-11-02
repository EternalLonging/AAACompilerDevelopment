const {createRequire}=require('node:module');
const {pathToFileURL}=require('node:url');
const assert=require('node:assert/strict');
const runtime=process.env.PLAYWRIGHT_MODULE_ROOT?createRequire(require('node:path').join(process.env.PLAYWRIGHT_MODULE_ROOT,'placeholder.cjs')):require;
const {chromium}=runtime('playwright');
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 const page=await browser.newPage({viewport:{width:1440,height:1100},reducedMotion:'reduce'}),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.goto(pathToFileURL(__dirname+'/reduce-actions.html').href);
 const report=await page.evaluate(()=>{
  const api=window.reduceDemo,data=api.data;let scenes=0,frames=0;
  if(data.groups.length!==111)throw Error('应覆盖 110 条产生式及收尾');
  const has=new Set(data.groups.map(g=>g.pid));
  for(let p=1;p<=110;p++)if(!has.has('P'+String(p).padStart(3,'0')))throw Error('缺产生式 '+p);
  function graphCheck(graph){for(const[id,n]of Object.entries(graph)){if(id!==n.id)throw Error('编号不符');for(const c of n.children)if(!graph[c])throw Error('孩子不存在 '+c);const seen=new Set();function visit(k){if(seen.has(k))throw Error('AST 成环');seen.add(k);graph[k].children.forEach(visit);seen.delete(k);}visit(id);}}
  data.groups.forEach((g,gi)=>g.scenes.forEach((s,vi)=>{
   scenes++;if(s.inputs.length!==s.count)throw Error('右部 Value 数不符');
   graphCheck(s.before);s.steps.forEach(t=>graphCheck(t.graph));
   for(const n of s.output.nodes)if(!s.steps.at(-1).graph[n])throw Error('返回节点丢失');
   api.select(gi,vi);const count=api.getState().total;
   for(let i=0;i<count;i++){api.setStep(i);frames++;const st=api.getState().state;
    if(!document.querySelector('#note').textContent)throw Error('缺说明');
    if(g.pid!=='FINAL'){
     const expected=st.phase===3?1:st.phase===2?0:s.count;
     if(document.querySelectorAll('#stack-rail .vcard').length!==expected)throw Error('栈卡片数不符 '+g.pid+'/'+i);
    }
    if(document.querySelectorAll('#tree .tree-node').length>Object.keys(st.graph).length)throw Error('重复树节点');
   }
  }));
  // 所有顶层片段的最终 AST 均应完整，不残留待补的运算、后缀或声明。
  const finals=data.groups.at(-1).scenes;
  for(const s of finals){const g=s.steps.at(-1).graph;for(const n of Object.values(g)){
   if(['Assign','BinaryOp','ArrayAccess'].includes(n.kind)&&n.children.length!==2)throw Error('二元节点不完整');
   if(['UnaryOp','MemberAccess'].includes(n.kind)&&n.children.length!==1)throw Error('一元节点不完整');
   if(n.kind==='For'&&n.children.length!==4)throw Error('For 孩子顺序不完整');
  }}
  return{productions:data.totalProductions,scenes,frames};
 });
 assert.deepEqual(errors,[]);
 await page.evaluate(()=>reduceDemo.select(36,0));
 await page.locator('#next').click();assert.equal((await page.evaluate(()=>reduceDemo.getState())).stepIndex,1);
 await page.locator('#prev').click();assert.equal((await page.evaluate(()=>reduceDemo.getState())).stepIndex,0);
 await page.locator('#speed').selectOption('650');await page.locator('#play').click();await page.waitForTimeout(750);
 assert.ok((await page.evaluate(()=>reduceDemo.getState())).stepIndex>=1);await page.locator('#play').click();
 const paused=(await page.evaluate(()=>reduceDemo.getState())).stepIndex;await page.waitForTimeout(750);assert.equal((await page.evaluate(()=>reduceDemo.getState())).stepIndex,paused);
 await page.locator('#search').fill('形参续');assert.equal(await page.locator('#nav .nav-item').count(),3);
 await page.locator('#search').fill('P079');assert.equal(await page.locator('#nav .nav-item').count(),1);
 await page.locator('#search').fill('');await page.locator('#category').selectOption('程序收尾');assert.equal(await page.locator('#nav .nav-item').count(),1);
 await page.locator('#category').selectOption('全部');
 await page.evaluate(()=>{reduceDemo.select(77,0);reduceDemo.setStep(3);});
 await page.screenshot({path:__dirname+'/reduce-actions-expression.png',fullPage:true});
 await page.evaluate(()=>{reduceDemo.select(36,0);reduceDemo.setStep(1);});
 await page.screenshot({path:__dirname+'/reduce-actions-parameters.png',fullPage:true});
 const node=page.locator('#tree .tree-node').first();await node.click();assert.match(await page.locator('#inspector').textContent(),/children =/);
 await page.setViewportSize({width:390,height:900});
 for(const gi of [0,10,36,49,77,110]){
  await page.evaluate(i=>{reduceDemo.select(i,0);reduceDemo.setStep(reduceDemo.getState().total-1);},gi);
  const size=await page.evaluate(()=>({client:document.documentElement.clientWidth,scroll:document.documentElement.scrollWidth}));
  assert.ok(size.scroll<=size.client+1,JSON.stringify({gi,...size}));
 }
 await page.evaluate(()=>{reduceDemo.select(36,0);reduceDemo.setStep(1);});await page.screenshot({path:__dirname+'/reduce-actions-mobile.png',fullPage:true});
 assert.deepEqual(errors,[]);console.log(JSON.stringify({...report,errors}));await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
