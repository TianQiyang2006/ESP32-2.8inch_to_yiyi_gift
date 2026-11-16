#include "days_store.h"
#include "days_logic.h"
#include "../gift_data.h"
#include <limits.h>

namespace DaysStore {
bool mutate(JsonVariantConst request, String &error) {
    if (!request.is<JsonObjectConst>() || !request["action"].is<const char*>()) {
        error="倒数日操作格式错误"; return false;
    }
    const String action=request["action"].as<String>();
    if (action!="upsert" && action!="remove") { error="不支持的倒数日操作"; return false; }
    if (request.size()!=2 || (action=="upsert" ? !request.containsKey("item") : !request.containsKey("id"))) {
        error="倒数日操作字段错误"; return false;
    }
    JsonVariantConst incoming=request["item"];
    JsonVariantConst idValue=action=="upsert"?incoming["id"]:request["id"];
    if (!idValue.is<int>() || idValue.as<int>()<(action=="upsert"?0:1)) {
        error="倒数日编号无效"; return false;
    }
    const int id=idValue.as<int>();
    JsonDocument patch;
    auto items=patch["countdownDays"]["items"].to<JsonArray>();
    for (JsonVariantConst item:GiftData::get("countdownDays")["items"].as<JsonArrayConst>()) items.add(item);
    int found=-1, maximum=0;
    for (unsigned i=0; i<items.size(); ++i) {
        const int stored=items[i]["id"].as<int>();
        if (stored==id) found=(int)i;
        if (stored>maximum) maximum=stored;
    }
    if (id && found<0) { error="这条倒数日已不存在，请刷新后重试"; return false; }
    if (action=="remove") {
        items.remove((unsigned)found);
    } else {
        const bool hasMode=incoming.containsKey("mode");
        const bool hasCategory=incoming.containsKey("category");
        const bool hasStyle=incoming.containsKey("style");
        const bool hasTextColor=incoming.containsKey("textColor");
        if (!incoming.is<JsonObjectConst>() || incoming.size()!=5u+hasMode+hasCategory+hasStyle+hasTextColor ||
            !incoming.containsKey("id") || !incoming.containsKey("name") ||
            !incoming.containsKey("date") || !incoming.containsKey("yearly") || !incoming.containsKey("color")) {
            error="倒数日需要完整的名称、日期、重复与配色"; return false;
        }
        if (!incoming["name"].is<const char*>() ||
            incoming["name"].as<JsonString>().size()!=strlen(incoming["name"].as<const char*>())) {
            error="倒数日名称无效"; return false;
        }
        if (!id && items.size()>=DaysLogic::MaxItems) { error="最多保存20条倒数日，请先删除一条"; return false; }
        if (!id && maximum==INT_MAX) { error="倒数日编号已用完"; return false; }
        JsonDocument canonical;canonical.set(incoming);
        String name=incoming["name"].as<String>();name.trim();
        canonical["name"]=name;
        canonical["id"]=id?id:maximum+1;
        if (id) items[(unsigned)found].set(canonical.as<JsonVariantConst>());
        else items.add(canonical.as<JsonVariantConst>());
    }
    return GiftData::patch(patch.as<JsonVariantConst>(),error);
}
} // namespace DaysStore
