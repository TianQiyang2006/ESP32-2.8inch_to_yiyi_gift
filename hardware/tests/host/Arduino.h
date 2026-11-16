#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <type_traits>
#include <ctime>
#define PROGMEM
#define ADC_11db 3
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
class String:public std::string {
public:
 using std::string::operator=;using std::string::operator+=;
 String &operator=(const char *value){assign(value?value:"");return *this;}
 String():std::string(){} String(const std::string &value):std::string(value){}
 String(const char *value):std::string(value?value:""){}String(char value):std::string(1,value){}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>String(T value):std::string(std::to_string(value)){}
 String(double value,unsigned char precision=2){std::ostringstream s;s<<std::fixed<<std::setprecision(precision)<<value;assign(s.str());}
 bool isEmpty()const{return empty();}bool reserve(size_t size){std::string::reserve(size);return true;}
 bool concat(const char *bytes,size_t size){append(bytes,size);return true;}bool concat(const char *bytes){append(bytes);return true;}
 void trim(){size_t begin=0,end=size();while(begin<end&&std::isspace((unsigned char)(*this)[begin]))++begin;while(end>begin&&std::isspace((unsigned char)(*this)[end-1]))--end;if(begin||end<size())assign(substr(begin,end-begin));}
 String substring(size_t from,size_t to=std::string::npos)const{return from>=size()?String():String(substr(from,to==std::string::npos?to:to-from));}
 bool startsWith(const String &prefix)const{return compare(0,prefix.size(),prefix)==0;}
 bool endsWith(const char *suffix)const{size_t n=strlen(suffix);return n<=size()&&compare(size()-n,n,suffix)==0;}
 bool endsWith(const String &suffix)const{return endsWith(suffix.c_str());}
 char charAt(size_t pos)const{return pos<size()?(*this)[pos]:0;}
 void setCharAt(size_t pos,char value){if(pos<size())(*this)[pos]=value;}
 void remove(size_t pos){if(pos<size())erase(pos);}
 void remove(size_t pos,size_t count){if(pos<size())erase(pos,count);}
 void toUpperCase(){for(auto &c:*this)c=(char)std::toupper((unsigned char)c);}
 void toLowerCase(){for(auto &c:*this)c=(char)std::tolower((unsigned char)c);}
 int indexOf(char value)const{auto pos=find(value);return pos==npos?-1:(int)pos;}int indexOf(const char *value)const{auto pos=find(value);return pos==npos?-1:(int)pos;}int indexOf(const String &value)const{auto pos=find(value);return pos==npos?-1:(int)pos;}
 int lastIndexOf(char value)const{auto pos=rfind(value);return pos==npos?-1:(int)pos;}int lastIndexOf(const char *value)const{auto pos=rfind(value);return pos==npos?-1:(int)pos;}int lastIndexOf(const String &value)const{auto pos=rfind(value);return pos==npos?-1:(int)pos;}
 void replace(char before,char after){for(auto &c:*this)if(c==before)c=after;}
 void replace(const char *before,const char *after){size_t pos=0,n=strlen(before);if(!n)return;while((pos=find(before,pos))!=npos){std::string::replace(pos,n,after);pos+=strlen(after);}}
 friend String operator+(const String &a,const String &b){String c=a;c.append(b);return c;}
 friend String operator+(const char *a,const String &b){return String(a)+b;}
 friend String operator+(const String &a,const char *b){return a+String(b);}
 friend String operator+(const String &a,char b){String c=a;c.push_back(b);return c;}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0>friend String operator+(const String &a,T b){return a+String(b);}
 long toInt()const{return strtol(c_str(),nullptr,10);}float toFloat()const{return strtof(c_str(),nullptr);}
};
extern uint32_t HostMillis;extern int HostBatteryMv;
inline uint32_t millis(){return HostMillis;}inline void delay(unsigned ms){HostMillis+=ms;}
template<class T,class A,class B>T constrain(T value,A low,B high){return std::max<T>((T)low,std::min<T>((T)high,value));}
inline void analogReadResolution(int){}inline void analogSetPinAttenuation(int,int){}
inline uint32_t (*HostBatteryReader)(int)=nullptr;
inline uint32_t analogReadMilliVolts(int pin){return HostBatteryReader?HostBatteryReader(pin):HostBatteryMv;}inline void pinMode(int,int){}inline void digitalWrite(int,int){}
