(function () {
  'use strict';
  // Lunar New Year dates 1900–2100, generated from lunar-javascript 1.7.7.
  // Kept offline: no browser-specific Chinese-calendar calculation or network.
  // Attribution and verification fixtures: calendar-data-source.md.
  const firstYear=1900;
  const springDates=(
    '0131 0219 0208 0129 0216 0204 0125 0213 0202 0122 0210 0130 0218 0206 0126 0214 0203 0123 0211 0201 ' +
    '0220 0208 0128 0216 0205 0124 0213 0202 0123 0210 0130 0217 0206 0126 0214 0204 0124 0211 0131 0219 ' +
    '0208 0127 0215 0205 0125 0213 0202 0122 0210 0129 0217 0206 0127 0214 0203 0124 0212 0131 0218 0208 ' +
    '0128 0215 0205 0125 0213 0202 0121 0209 0130 0217 0206 0127 0215 0203 0123 0211 0131 0218 0207 0128 ' +
    '0216 0205 0125 0213 0202 0220 0209 0129 0217 0206 0127 0215 0204 0123 0210 0131 0219 0207 0128 0216 ' +
    '0205 0124 0212 0201 0122 0209 0129 0218 0207 0126 0214 0203 0123 0210 0131 0219 0208 0128 0216 0205 ' +
    '0125 0212 0201 0122 0210 0129 0217 0206 0126 0213 0203 0123 0211 0131 0219 0208 0128 0215 0204 0124 ' +
    '0212 0201 0122 0210 0130 0217 0206 0126 0214 0202 0123 0211 0201 0219 0208 0128 0215 0204 0124 0212 ' +
    '0202 0121 0209 0129 0217 0205 0126 0214 0203 0123 0211 0131 0219 0207 0127 0215 0205 0124 0212 0202 ' +
    '0122 0209 0129 0217 0206 0126 0214 0203 0124 0210 0130 0218 0207 0127 0215 0205 0125 0212 0201 0121 ' +
    '0209 '
  ).trim().split(/\s+/);
  const weekdays=['日','一','二','三','四','五','六'];
  const english=['SUNDAY','MONDAY','TUESDAY','WEDNESDAY','THURSDAY','FRIDAY','SATURDAY'];
  const key=(year,month,day)=>String(year).padStart(4,'0')+'-'+String(month).padStart(2,'0')+'-'+String(day).padStart(2,'0');
  // Civil-date ordinals deliberately ignore the hour and avoid rounding elapsed
  // milliseconds. Today is zero, including the entire festival day in Beijing.
  const ordinal=(year,month,day)=>Date.UTC(year,month-1,day)/86400000;
  function springDate(year) {
    const entry=springDates[year-firstYear];
    return entry?key(year,Number(entry.slice(0,2)),Number(entry.slice(2))):null;
  }
  function event(date,today) {
    if(!date)return {date:null,days:null};
    const [year,month,day]=date.split('-').map(Number);
    return {date,days:ordinal(year,month,day)-today};
  }
  function read(date,config={weekday:5,label:'放假'}) {
    const {year,month,day}=date,today=ordinal(year,month,day);
    const actualWeekday=new Date(today*86400000).getUTCDay();
    const target=Number.isInteger(config.weekday)&&config.weekday>=0&&config.weekday<=6?config.weekday:5;
    const remaining=(target-actualWeekday+7)%7;
    const nextYear=month===1&&day===1?year:year+1;
    let spring=springDate(year);
    if(spring && event(spring,today).days<0)spring=springDate(year+1);
    return {today:key(year,month,day),weekday:actualWeekday,targetWeekday:target,remaining,
      question:'今天是周'+weekdays[target]+'么？',
      answer:remaining===0?'是的，今天'+(config.label||'放假'):'不是，还有'+remaining+'天',
      english:english[target],
      targetDate:new Date((today+remaining)*86400000).toISOString().slice(0,10),
      newYear:event(key(nextYear,1,1),today),springFestival:event(spring,today)};
  }
  window.YiyiHolidayCalendar={read,springDate};
})();
