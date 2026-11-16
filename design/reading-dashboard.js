(function () {
  'use strict';
  const feed=window.YiyiReading;
  const feather='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M21 2C12-1 4 5 5 14L2 22l9-8c5 0 12-6 10-12Z" fill="currentColor"/><path d="M3 21 19 5" stroke="var(--reading-quote-card)" stroke-width="1.5"/></svg>';
  function label(value) {
    if(value.loading)return '更新中';
    const source=value.source==='manual'?'自定义':value.source==='example'?'示例':value.current?'今日':'缓存';
    return source+(value.error?' · 更新失败':'');
  }
  function author(value,kind) {
    return kind==='poem'?[value.dynasty?(value.dynasty.endsWith('代')?value.dynasty:value.dynasty+'代'):'',value.author||'作者未提供'].filter(Boolean).join(' · '):value.author||'作者未提供';
  }
  function card(kind) {
    const value=feed.read(kind),poem=kind==='poem';
    const lines=poem?value.text.replace(/([，。！？；])(?=[^\n])/g,'$1\n').split('\n').map(line=>line.trim()).filter(Boolean).slice(0,2).join('\n'):value.text;
    return '<article class="reading-'+kind+'" aria-label="'+(poem?'诗句':'每日名言')+'"><span class="reading-marks" aria-hidden="true">“</span>'+
      (!poem?'<time class="reading-date">'+dateText()+'</time>':'')+
      '<button class="reading-refresh" data-action="reading-refresh-'+kind+'" aria-label="刷新'+(poem?'诗句':'名言')+'"'+(value.loading||value.source==='manual'?' disabled':'')+' title="'+esc(value.error||'点击获取新内容')+'">'+esc(label(value))+(value.source==='manual'?'':' ↻')+'</button>'+
      '<button class="reading-content" data-action="reading-detail-'+kind+'" aria-label="阅读完整'+(poem?'诗词':'名言')+'"><span class="reading-text">'+esc(lines||'请在内容面板填写文字')+'</span></button>'+
      '<span class="reading-author">'+(!poem?feather:'<i aria-hidden="true"></i>')+'<span>'+esc(author(value,kind))+'</span></span></article>';
  }
  function dateText() {const time=window.YiyiData.time();return String(time.month).padStart(2,'0')+'/'+String(time.day).padStart(2,'0');}
  function source(value,kind) {
    if(value.source==='manual')return '手动编辑的内容';
    if(value.source==='example')return '参考图示例，尚未获取接口内容';
    return kind==='quote'?(value.provider==='uapis'?'来源：UApi 一言':'来源：历史接口缓存'):'来源：Palemoky 诗词 API';
  }
  function details(kind) {
    const value=feed.read(kind),time=value.updatedAt!==null?window.YiyiData.time(new Date(value.updatedAt)):null;
    return '<section class="reading-detail"><div class="reading-detail-nav"><button data-action="back">‹ 返回</button><b>'+esc(kind==='poem'?value.title||'诗词全文':'每日名言')+'</b><button data-action="reading-refresh-'+kind+'"'+(value.loading||value.source==='manual'?' disabled':'')+'>↻ 刷新</button></div><div class="reading-detail-body scroll-body"><p class="reading-full-text">'+esc(value.text||'尚未填写文字')+'</p><p class="reading-full-author">'+esc(author(value,kind))+'</p><p class="reading-source">'+esc(label(value))+(time?' · '+value.date+' '+time.clock+' 更新':'')+'<br>'+esc(source(value,kind))+'</p>'+(value.error?'<p class="reading-error" role="status">'+esc(value.error)+'；已有内容保留。</p>':'')+'</div></section>';
  }
  function markup(section) {
    if(section==='reading-detail-quote')return details('quote');
    if(section==='reading-detail-poem')return details('poem');
    return '<section class="reading-dashboard" aria-label="每日名言与诗句">'+card('quote')+card('poem')+'</section>';
  }
  function fit(root) {
    root.querySelectorAll('.reading-content').forEach(button=>{
      const text=button.querySelector('.reading-text'),card=button.closest('article');
      const base=parseFloat(card.style.fontSize)|| (card.classList.contains('reading-poem')?19:16),length=text.textContent.length;
      text.style.fontSize=Math.max(8,Math.min(base,length>90?11:length>55?13:base))+'px';
    });
  }
  function refresh(root,{content=false}={}) {
    const date=root.querySelector('.reading-date');if(date)date.textContent=dateText();
    if(!content)return;
    const dashboard=root.querySelector('.reading-dashboard');
    if(dashboard)for(const kind of ['quote','poem']) {
      const previous=dashboard.querySelector('.reading-'+kind),wrapper=document.createElement('div');wrapper.innerHTML=card(kind);
      const current=wrapper.firstElementChild;current.style.cssText=previous.style.cssText;current.className=previous.className;
      if(previous.dataset.designLayer)current.dataset.designLayer=previous.dataset.designLayer;
      previous.replaceWith(current);
    }
    const detail=root.querySelector('.reading-detail');
    if(detail && state.section?.startsWith('reading-detail-'))detail.outerHTML=details(state.section.slice(15));
    fit(root);
  }
  function handle(action) {
    if(!action.startsWith('reading-'))return false;
    if(action.startsWith('reading-detail-')) {state.section=action;render();}
    if(action.startsWith('reading-refresh-')) {
      const kind=action.slice(16),value=feed.read(kind);
      if(Date.now()<value.retryAt&&!value.loading)notify('请稍后再刷新。');else void feed.refresh(kind);
    }
    resetIdle();return true;
  }
  window.YiyiReadingUI={markup,refresh,fit,handle};
})();
