#include "schedule_store.h"
#include "schedule_data.h"
#include "gift_assets/gift_glyph_coverage.h"
#include <LittleFS.h>
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr int MaxCourses = 64;
constexpr const char *FilePath = "/schedule.txt";
constexpr const char *TempPath = "/schedule.tmp";
constexpr const char *BackupPath = "/schedule.bak";

ScheduleStore::Course courses[MaxCourses];
int courseCount = 0;
char term[12] = "";
char starts[12][7] = {};
char ends[12][7] = {};
uint8_t total = 20;
bool loaded = false;
bool fromFile = false;
char errorText[64] = "";

void copyUtf8(char *target, size_t size, const char *source) {
    if (!source) source = "";
    size_t length = strlen(source);
    if (length >= size) {
        length = size - 1;
        while (length > 0 && (static_cast<unsigned char>(source[length]) & 0xc0) == 0x80) --length;
    }
    memcpy(target, source, length);
    target[length] = 0;
}

void fail(const char *message) {
    snprintf(errorText, sizeof(errorText), "%s", message);
}

// Imported course/teacher/room text is rendered with the F-d fonts, so every
// visible codepoint must be in their union coverage. Newlines belong to the
// schedule file format (not to a label) and are therefore skipped.
bool glyphsCovered(const char *utf8) {
    if (!utf8) return true;
    for (const unsigned char *p=(const unsigned char*)utf8; *p;) {
        uint32_t cp; const unsigned char c=*p++;
        if (c<0x80) cp=c;
        else if ((c&0xE0)==0xC0) { if ((*p&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x1F)<<6)|(*p++&0x3F); }
        else if ((c&0xF0)==0xE0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x0F)<<12)|((uint32_t)(p[0]&0x3F)<<6)|(p[1]&0x3F); p+=2; }
        else if ((c&0xF8)==0xF0) { if ((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80||(p[2]&0xC0)!=0x80) return false; cp=((uint32_t)(c&0x07)<<18)|((uint32_t)(p[0]&0x3F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F); p+=3; }
        else return false;
        if (cp=='\n'||cp=='\r'||cp=='\t') continue;
        if (!GiftCoverage::has(cp)) return false;
    }
    return true;
}

void loadBaked() {
    courseCount = kScheduleCourseCount < MaxCourses ? kScheduleCourseCount : MaxCourses;
    for (int index = 0; index < courseCount; ++index) {
        const ScheduleCourse &source = kScheduleCourses[index];
        ScheduleStore::Course &target = courses[index];
        copyUtf8(target.name, sizeof(target.name), source.name);
        copyUtf8(target.teacher, sizeof(target.teacher), source.teacher);
        copyUtf8(target.room, sizeof(target.room), source.room);
        target.color = source.color;
        target.day = source.day;
        target.start = source.start;
        target.end = source.end;
        target.weeks = source.weeks;
    }
    copyUtf8(term, sizeof(term), kScheduleTermStart);
    total = kScheduleTotalWeeks;
    for (int node = 0; node < 12; ++node) {
        copyUtf8(starts[node], sizeof(starts[node]), kSchedulePeriodStart[node]);
        copyUtf8(ends[node], sizeof(ends[node]), kSchedulePeriodEnd[node]);
    }
    fromFile = false;
}

bool parse(const char *text) {
    courseCount = 0;
    total = 20;
    term[0] = 0;
    for (int node = 0; node < 12; ++node) {
        starts[node][0] = 0;
        ends[node][0] = 0;
    }
    char line[320];
    const char *cursor = text;
    while (*cursor) {
        const char *newline = strchr(cursor, '\n');
        size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
        if (length >= sizeof(line)) {
            fail("课表行过长");
            return false;
        }
        memcpy(line, cursor, length);
        line[length] = 0;
        while (length && line[length - 1] == '\r') line[--length] = 0;
        if (!length) { cursor = newline ? newline + 1 : cursor + length; continue; }
        if (!strncmp(line, "TERM|", 5)) {
            unsigned weeks = 0;
            if (sscanf(line + 5, "%11[^|]|%u", term, &weeks) != 2 || weeks < 1 || weeks > 32) {
                fail("学期信息无效");
                return false;
            }
            total = (uint8_t)weeks;
        } else if (!strncmp(line, "P|", 2)) {
            unsigned node = 0;
            char begin[8] = "", finish[8] = "";
            if (sscanf(line + 2, "%u|%7[^|]|%7[^|]", &node, begin, finish) != 3 || node < 1 || node > 12) {
                fail("节次时间无效");
                return false;
            }
            copyUtf8(starts[node - 1], sizeof(starts[node - 1]), begin);
            copyUtf8(ends[node - 1], sizeof(ends[node - 1]), finish);
        } else if (!strncmp(line, "C|", 2)) {
            if (courseCount >= MaxCourses) {
                fail("课程过多");
                return false;
            }
            char *parts[10] = {};
            int count = 0;
            parts[count++] = line;
            for (char *p = line; *p && count < 10; ++p)
                if (*p == '|') { *p = 0; parts[count++] = p + 1; }
            if (count != 9) {
                fail("课程行格式错误");
                return false;
            }
            ScheduleStore::Course &target = courses[courseCount];
            int day = atoi(parts[1]);
            int start = atoi(parts[2]);
            int end = atoi(parts[3]);
            unsigned long color = strtoul(parts[4], nullptr, 0);
            unsigned long weeks = strtoul(parts[5], nullptr, 0);
            if (day < 1 || day > 7 || start < 1 || start > 12 || end < start || end > 12 ||
                color > 0xffffff || !weeks) {
                fail("课程字段超出范围");
                return false;
            }
            copyUtf8(target.name, sizeof(target.name), parts[6]);
            copyUtf8(target.teacher, sizeof(target.teacher), parts[7]);
            copyUtf8(target.room, sizeof(target.room), parts[8]);
            if (!target.name[0]) {
                fail("课程名称为空");
                return false;
            }
            target.color = (uint32_t)color;
            target.day = (uint8_t)day;
            target.start = (uint8_t)start;
            target.end = (uint8_t)end;
            target.weeks = (uint32_t)weeks;
            ++courseCount;
        } else if (strncmp(line, "V1", 2)) {
            fail("未知的课表行");
            return false;
        }
        cursor = newline ? newline + 1 : cursor + length;
    }
    if (!courseCount) {
        fail("课表没有课程");
        return false;
    }
    if (!term[0]) copyUtf8(term, sizeof(term), kScheduleTermStart);
    for (int node = 0; node < 12; ++node) {
        if (!starts[node][0]) copyUtf8(starts[node], sizeof(starts[node]), kSchedulePeriodStart[node]);
        if (!ends[node][0]) copyUtf8(ends[node], sizeof(ends[node]), kSchedulePeriodEnd[node]);
    }
    return true;
}

bool readFile(const char *path, String &text) {
    text = "";
    File file = LittleFS.open(path, "r");
    if (!file || !file.size() || file.size() > 24000) {
        if (file) file.close();
        return false;
    }
    const size_t expected = file.size();
    if (!text.reserve(expected)) { file.close(); return false; }
    while (file.available()) text += (char)file.read();
    file.close();
    return text.length() == expected;
}
}

namespace ScheduleStore {
void begin() {
    if (!loaded) load();
}

bool load() {
    loaded = true;
    fromFile = false;
    String text;
    for (const char *path : {FilePath, BackupPath}) {
        if (readFile(path, text) && parse(text.c_str())) {
            fromFile = true;
            fail("");
            return true;
        }
    }
    loadBaked();
    return false;
}

int count() { begin(); return courseCount; }
const Course &course(int index) { begin(); return courses[index]; }
const char *termStart() { begin(); return term; }
uint8_t totalWeeks() { begin(); return total; }
const char *periodStart(int node) { begin(); return (node >= 1 && node <= 12) ? starts[node - 1] : ""; }
const char *periodEnd(int node) { begin(); return (node >= 1 && node <= 12) ? ends[node - 1] : ""; }
const char *lastError() { return errorText; }
bool imported() { begin(); return fromFile; }

bool saveText(const char *text, unsigned length) {
    if (!text || length < 10 || length > 24000 || !strstr(text, "TERM|") || !strstr(text, "C|")) {
        fail("课表内容为空或格式错误");
        return false;
    }
    // Reject before touching the filesystem: the previous schedule stays intact.
    if (!glyphsCovered(text)) {
        fail("课表包含设备字库不支持的生僻字，请修改");
        return false;
    }
    File file = LittleFS.open(TempPath, "w");
    if (!file) {
        fail("无法写入课表文件");
        return false;
    }
    const size_t written = file.write(reinterpret_cast<const uint8_t *>(text), length);
    file.flush();
    file.close();
    String stored;
    if (written != length || !readFile(TempPath, stored) || stored.length() != length || !parse(stored.c_str())) {
        LittleFS.remove(TempPath);
        load();
        fail("课表写入不完整或格式错误，原课表保留");
        return false;
    }
    // Only replace a backup when the current main file is itself valid.
    String previous;
    const bool existing = LittleFS.exists(FilePath);
    const bool validMain = existing && readFile(FilePath, previous) && parse(previous.c_str());
    if (existing) {
        const bool prepared = validMain
            ? ((!LittleFS.exists(BackupPath) || LittleFS.remove(BackupPath)) && LittleFS.rename(FilePath, BackupPath))
            : LittleFS.remove(FilePath);
        if (!prepared) {
            LittleFS.remove(TempPath);
            load();
            fail("无法备份原课表");
            return false;
        }
    }
    if (!LittleFS.rename(TempPath, FilePath)) {
        if (validMain) LittleFS.rename(BackupPath, FilePath);
        LittleFS.remove(TempPath);
        load();
        fail("无法提交课表，已保留恢复副本");
        return false;
    }
    parse(stored.c_str());
    fromFile = true;
    loaded = true;
    fail("");
    return true;
}

bool clear() {
    // Remove recovery files first so a completed reset cannot resurrect them.
    for (const char *path : {TempPath, BackupPath, FilePath}) {
        if (LittleFS.exists(path) && !LittleFS.remove(path)) {
            load();
            fail("无法删除课表文件，请重试");
            return false;
        }
    }
    loadBaked();
    loaded = true;
    fromFile = false;
    fail("");
    return true;
}
}
