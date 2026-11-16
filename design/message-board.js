(function () {
  'use strict';

  let configuration = window.YiyiDesign.clone(window.YiyiDesign.defaults);
  let artURL = value => value;
  let boardReading = null;

  function letter() {
    const value = configuration.letter;
    return {...value,title:value.title.trim() || '给你的一封信',empty:!value.text.trim()};
  }

  function board() {
    const value = letter();
    return '<section class="message-board" aria-label="留言板" data-message-date="'+window.YiyiDaily.today()+'">'+
      '<img class="message-board-art" src="'+esc(artURL('assets/message-board-yellow.png'))+'" alt="" draggable="false">'+
      '<header class="message-board-heading"><h1 class="message-board-title">留言板<span aria-hidden="true">♡</span></h1></header>'+
      '<section class="message-note message-note-letter" aria-label="留言内容" data-empty="'+value.empty+'">'+
      '<div class="message-note-scroll scroll-body" tabindex="0" aria-label="留言正文">'+
      '<strong class="message-note-summary">'+esc(value.title)+'</strong>'+
      '<p class="message-note-preview">'+esc(value.empty ? '这里还没有留言。\n点击右上角“手机编辑”，写下想留下的话。' : value.text)+'</p>'+
      (value.signature && !value.empty ? '<footer class="message-note-sign">'+esc(value.signature)+'</footer>' : '')+'</div>'+
      '<button type="button" class="message-read" data-action="'+(value.empty ? 'connect-phone' : 'message-open-letter')+'"><span>'+(value.empty ? '写一条留言' : '阅读全文')+'</span><span aria-hidden="true">↗</span></button></section>'+
      '<button type="button" class="message-phone" data-action="connect-phone">手机编辑 <span aria-hidden="true">↗</span></button></section>';
  }

  function detail() {
    const value = letter();
    return '<section class="message-detail" data-message-kind="letter">'+
      '<img class="message-detail-art" src="'+esc(artURL('assets/message-board-yellow.png'))+'" alt="" draggable="false">'+
      '<nav class="message-detail-nav" aria-label="留言操作"><button type="button" data-action="message-back" aria-label="返回留言板">‹ 返回</button><span>留言</span><button type="button" data-action="connect-phone">手机编辑</button></nav>'+
      '<article class="message-reader"><div class="message-text scroll-body" tabindex="0" aria-label="留言完整内容">'+
      '<header><h2>'+esc(value.title)+'</h2></header>'+
      '<div class="message-body'+(value.empty ? ' message-empty' : '')+'">'+esc(value.empty ? '这里还没有留言。\n在手机上写一条留言吧。' : value.text)+'</div>'+
      (value.empty ? '<button type="button" class="message-empty-action" data-action="connect-phone">写一条留言 ›</button>' : '')+
      (value.signature && !value.empty ? '<footer class="message-sign">'+esc(value.signature)+'</footer>' : '')+'</div></article></section>';
  }

  function markup() { return state.section === 'message-letter' ? detail() : board(); }
  function configure(next,resolve) { configuration = next;artURL = resolve || (value => value); }
  function refresh(root) {
    const board = root.querySelector('.message-board');
    if (board) board.dataset.messageDate = window.YiyiDaily.today();
  }
  function reader() { return screen.querySelector('.message-note-scroll,.message-detail .message-text'); }
  function position() { const element = reader();return {top:element?.scrollTop || 0,text:element?.textContent}; }
  function restore(previous) { const element = reader();if (element && element.textContent === previous?.text) element.scrollTop = previous.top; }

  function handle(action) {
    if (action === 'message-open-letter') {
      boardReading = position();
      state.section = 'message-letter';
      render();
      reader()?.focus({preventScroll:true});
      return true;
    }
    if (action === 'message-back') {
      state.section = null;
      render();
      restore(boardReading);
      screen.querySelector('[data-action="message-open-letter"]')?.focus({preventScroll:true});
      return true;
    }
    if (action === 'connect-phone' && !state.app && state.page === 5) {
      state.messagePhoneReturn = {section:state.section,...position(),action:document.activeElement?.dataset.action};
      state.app = 'settings';
      state.section = 'phone';
      render();
      return true;
    }
    if (action === 'back' && state.app === 'settings' && state.section === 'phone' && state.messagePhoneReturn) {
      const previous = state.messagePhoneReturn;
      state.messagePhoneReturn = null;
      state.page = 5;
      state.app = null;
      state.section = previous.section;
      render();
      restore(previous);
      const focus = previous.action && screen.querySelector('[data-action="'+previous.action+'"]');
      (focus || reader())?.focus({preventScroll:true});
      return true;
    }
    return false;
  }

  document.addEventListener('keydown',event => {
    if (event.key === 'Escape' && !screen.classList.contains('editing') && !state.voice && !state.app && state.page === 5 && state.section === 'message-letter') {
      event.preventDefault();handle('message-back');resetIdle();
    }
  });
  window.YiyiMessageUI = {configure,markup,refresh,handle};
})();
