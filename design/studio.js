'use strict';
const implementation = {
  home:{
    title:'主页 · 天气时钟', status:'常规功能可实现',
    behavior:'大号时间优先，小黑与天气为陪伴信息。天气离线时保留缓存，显示更新时间。',
    needs:'屏幕驱动、2.4GHz Wi-Fi、网络校时、天气 API，以及小黑素材。',
    check:'无 PSRAM 时也可精简实现。角色采用少量帧和局部刷新，实际动画速度在实板验证。'
  },
  album:{
    title:'相册 · TF 照片', status:'可实现，需读卡与图片解码',
    behavior:'按文件名排序、保持比例展示、循环翻页与轮播。单独打开相册时，横滑翻照片。',
    needs:'TF 卡、TF 引脚和驱动、JPEG 解码器、提前裁剪或缩小的照片。',
    check:'原型可导入本地照片。实板优先 JPEG；大图、PNG、预加载速度取决于解码器和内存。'
  },
  letter:{
    title:'留言 · 小黑主题', status:'可实现，需中文字体',
    behavior:'第三页展示留言；应用负责选择主题和内容。长文滚动，更新时重新读取 TF JSON。',
    needs:'UTF-8 留言文件、主题素材、覆盖新增汉字的字库、保存所选主题与留言的配置。',
    check:'普通字体可实现，但浏览器中文字库不等于固件字库。正式版本要验证缺字和可读性。'
  },
  countdown:{
    title:'倒计时 · 下一件期待的事', status:'可实现，无需新增硬件',
    behavior:'自定义名称和目标时间，剩余天数与时分秒实时更新。到期显示“已到达”。',
    needs:'准确的当前时间、目标时间配置；第一版通过联网校时。',
    check:'断电后无网络、也没有可用保时时钟时，需要重新校时。若要求完全离线保时，再评估 RTC 模块。'
  },
  calendar:{
    title:'日历 · 查看与设置', status:'公历可实现，无需联网查询日期',
    behavior:'切换月份、选日期、回到今天；选择日期后设置倒计时。当前支持一个目标。',
    needs:'日期计算和触摸驱动；手机配置页可协助输入长名称。',
    check:'农历、节气和法定调休尚未加入。月历格子较小，实际点选手感需要屏幕验证。'
  },
  apps:{
    title:'全部应用 · 六个入口', status:'可实现，优先静态主题背景',
    behavior:'三列两行排列，相册、留言、天气、小智、日历和设置。背景可切为纯白。',
    needs:'统一图标、320×240 背景图、足够对比度的文字与触摸区域。',
    check:'RGB565 全屏背景约 150KiB。与小智联合运行的内存余量未确认，支持按需加载和纯色后备。'
  },
  weather:{
    title:'天气 · 详情与地区', status:'可实现，定位方式待选',
    behavior:'温度、湿度、风和预报分区显示，额外信息滚动查看；手机定位与手动选地区并存。',
    needs:'天气 API 与地区查询、图标素材；手机定位需授权及合适的 HTTPS 页面或手机端配合。',
    check:'设备热点上的普通 HTTP 网页通常不能直接获取浏览器位置。天气服务、额度和配网通信还需确定。'
  },
  voice:{
    title:'小智 · 语音对话', status:'需要对应板型的小智源码',
    behavior:'唤醒切进小智流程，退出后恢复之前的应用和位置。语音期间暂停轮播。',
    needs:'麦克风与音频驱动、喇叭、小智板型配置、Wi-Fi 与对话服务。',
    check:'资料尚未确认 PSRAM。语音、显示与 TF 同时工作的内存及总线负载必须在实板测试。'
  },
  settings:{
    title:'设置 · 设备与手机', status:'基础设置可实现，通信待接入',
    behavior:'亮度、音量、Wi-Fi、手机配置、TF 容量和关于信息。滑块更新后合并保存配置。',
    needs:'背光与音频控制接口、存储容量查询、小智配网实现，以及设备配置存储。',
    check:'蓝牙用途尚未确定；S3 支持 BLE，不能直接作为经典蓝牙音箱。当前各项硬件状态仅为演示。'
  }
};
function updateDesignPanel() {
  const key = state.voice ? 'voice' : state.app ||
    ['home','album','letter','countdown','apps'][state.page];
  const info = implementation[key] || implementation.home;
  document.getElementById('implementation-title').textContent = info.title;
  document.getElementById('implementation-status').textContent = info.status;
  document.getElementById('implementation-behavior').textContent = info.behavior;
  document.getElementById('implementation-needs').textContent = info.needs;
  document.getElementById('implementation-check').textContent = info.check;
  document.querySelectorAll('[data-preview-page]').forEach(button => {
    button.setAttribute('aria-pressed',String(!state.voice && !state.app &&
      state.page === Number(button.dataset.previewPage)));
    button.disabled = state.voice;
  });
}
document.querySelectorAll('[data-preview-page]').forEach(button => {
  button.addEventListener('click',() => {
    if (state.voice) return;
    state.app = null; state.section = null; state.slideshow = false;
    state.page = Number(button.dataset.previewPage);
    render(); resetIdle();
  });
});
document.getElementById('design-mode').addEventListener('change',event => {
  state.designMode = event.target.checked; resetIdle();
  notify(state.designMode ? '设计模式：保持当前页面，便于比较布局。' : '已恢复闲置 60 秒回主页。');
});
updateDesignPanel();
