# 第三页春节日期数据

`holiday-calendar.js` 内置 1900～2100 年农历正月初一对应的公历日期，索引为公历年减 1900。数据由 [lunar-javascript 1.7.7](https://www.npmjs.com/package/lunar-javascript/v/1.7.7) 的 `Lunar.fromYmd(year, 1, 1).getSolar().toYmd()` 生成；运行和独立 HTML 导出无需加载第三方库或联网。

没有用浏览器的 Chinese calendar 动态推算日期：部分 ICU 版本会把 2027 年春节算成 2 月 7 日。已通过香港天文台的 [2027 年公历与农历对照表](https://www.hko.gov.hk/en/gts/time/calendar/text/files/T2027e.txt) 确认是 **2027-02-06**，并通过 [2034 年对照表](https://www.hko.gov.hk/en/gts/time/calendar/text/files/T2034e.txt) 确认是 **2034-02-19**。

回归用例包含 2024-02-10、2025-01-29、2026-02-17、2027-02-06、2028-01-26，以及 2033/2034 年与范围端点。超出支持年份显示未知，不用固定日期代替。工作台预览日期支持 1900～2099 年，因此当年春节已过时仍能计算下一年的春节。

星期与节日倒计时统一读取 `YiyiData.time()` 的北京时间日期，按民用日期序数相减，忽略当天小时。节日当天为 0 天，次日寻找下一次节日；每周目标当天为 0 天，次日计算到下周。这里的“放假”是用户设置的每周目标，不是法定调休安排。

验证命令：`node tools/design-review/check-holiday-calendar.cjs` 和 `node tools/design-review/check-holiday-page.cjs`。

## 数据生成所用项目许可

MIT License

Copyright (c) 2018 6tail

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
