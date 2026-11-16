#pragma once
namespace PixelClock {
constexpr int Columns=3, Rows=5, Cells=Columns*Rows, MainDigits=4, Digits=6;
constexpr int X=20, Y=49, Step=17, Halo=6, ColonX=131;
constexpr int SecondsX=130, SecondsY=160, SecondsStep=9, SecondsHalo=3;
constexpr int Positions[MainDigits]={0,65,164,231}, SecondsPositions[2]={0,34};
static const char *const Patterns[]={
    "111101101101111", "001001001001001", "111001111100111",
    "111001111001111", "101101111001001", "111100111001111",
    "111100111101111", "111001001001001", "111101111101111", "111101111001111"
};
inline bool lit(int digit,int cell){
    if(cell<0||cell>=Cells)return false;
    return digit>=0&&digit<=9?Patterns[digit][cell]=='1':cell>=6&&cell<=8;
}
}
