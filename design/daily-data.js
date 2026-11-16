(function () {
  'use strict';
  const model=window.YiyiDesign,clock=window.YiyiData,copy=model.clone;
  let options=copy(model.defaults.daily),records={};
  const listeners=new Set();
  const validDate=value=>/^\d{4}-\d{2}-\d{2}$/.test(value) && Number.isFinite(Date.parse(value+'T00:00:00Z')) && new Date(value+'T00:00:00Z').toISOString().slice(0,10)===value;
  const empty=()=>({mood:'',note:'',water:[],knocks:0});
  const today=()=>clock.time().key;
  const day=(key=today())=>copy(records[key]||empty());
  function configure(nextOptions,nextRecords) {options=copy(nextOptions);records=copy(nextRecords);}
  function publish(key) {listeners.forEach(fn=>fn({key,records:copy(records)}));}
  function edit(key,fn) {
    if(!validDate(key)||key>today())throw new Error('请选择今天或之前的日期。');
    const next=day(key);fn(next);records[key]=next;publish(key);return day(key);
  }
  function saveMood(key,mood,note) {
    if(!model.moods.some(item=>item.id===mood))throw new Error('先选择一种心情。');
    return edit(key,record=>{record.mood=mood;record.note=String(note||'').slice(0,3000);});
  }
  function deleteMood(key) {return edit(key,record=>{record.mood='';record.note='';});}
  function strike() {return edit(today(),record=>{record.knocks=Math.min(999999,record.knocks+1);});}
  function undoStrike() {return edit(today(),record=>{record.knocks=Math.max(0,record.knocks-1);});}
  function addWater(amount) {
    if(!Number.isInteger(amount)||amount<1||amount>3000)throw new Error('每次饮水量请输入 1～3000 ml 的整数。');
    return edit(today(),record=>{if(record.water.length>=200)throw new Error('今天已记录 200 次，可先撤销误记。');record.water.push({amount,at:clock.now().getTime()});});
  }
  function undoWater() {return edit(today(),record=>{record.water.pop();});}
  function summary(key=today()) {
    const record=day(key),water=record.water.reduce((sum,item)=>sum+item.amount,0);
    return {...record,date:key,waterTotal:water,waterGoal:options.waterGoal,waterPercent:Math.round(water/options.waterGoal*100),totalKnocks:Object.values(records).reduce((sum,item)=>sum+item.knocks,0)};
  }
  function week() {
    const date=clock.time(),monday=new Date(Date.UTC(date.year,date.month-1,date.day-(date.weekday+6)%7));
    return Array.from({length:7},(_,index)=>{
      const instant=new Date(monday.getTime()+index*86400000),key=instant.toISOString().slice(0,10);
      return {key,label:'周'+['一','二','三','四','五','六','日'][index],date:(instant.getUTCMonth()+1)+'/'+instant.getUTCDate(),today:key===date.key,future:key>date.key,...day(key)};
    });
  }
  window.YiyiDaily={configure,today,day,week,summary,saveMood,deleteMood,strike,undoStrike,addWater,undoWater,snapshot:()=>copy(records),subscribe(fn){listeners.add(fn);return()=>listeners.delete(fn);}};
})();
