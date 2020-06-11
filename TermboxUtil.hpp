#ifndef TERMBOXUTIL_HPP
#define TERMBOXUTIL_HPP

#include <string>
#include <vector>
#include <cwctype>

#include <wchar.h>
#include "termbox/termbox.h"
#include "termbox/wcwidth-cjk.hpp"

std::wstring string2wstring(const std::string &s) {
  auto mbstr = s.c_str();
  std::mbstate_t state = std::mbstate_t();
  std::size_t len = 1 + std::mbsrtowcs(NULL, &mbstr, 0, &state);
  std::vector<wchar_t> wstr(len);
  std::mbsrtowcs(&wstr[0], &mbstr, wstr.size(), &state);

  return std::wstring(wstr.begin(), wstr.end());
}

std::string wstring2string(const std::wstring& ws)
{
  auto wstr = ws.c_str();
  std::mbstate_t state = std::mbstate_t();
  std::size_t len = 1 + std::wcsrtombs(nullptr, &wstr, 0, &state);
  std::vector<char> mbstr(len);
  std::wcsrtombs(&mbstr[0], &wstr, mbstr.size(), &state);

  return std::string(mbstr.begin(), mbstr.end());
}

std::string strimwidth(const std::string& str, int l, int* strlen = 0)
{
  auto wstr = string2wstring(str);

  int i, w = 0;
  for(i = 0; i < static_cast<int>(wstr.length()); ++i) {
    // escape sequence
    if(wstr[i] == L'\e') {
      if(static_cast<int>(wstr.length()) > i + 1 && wstr[i + 1] == L'[') {
        int j = 1;
        while(wstr[i + j] != L'm') {
          if(j < static_cast<int>(wstr.length())) ++j;
          else break;
        }
        i += j;
        continue;
      }
    }
    if(iswcntrl(wstr[i])) wstr[i] = L' ';

    auto wc = tb_wcwidth(wstr[i]);
    if(wc < 0) break;
    w += wc;

    if(w > l) {
      w -= wc;
      break;
    }
  }

  if(strlen != 0) *strlen = w;
  return wstring2string(std::wstring(wstr.begin(), wstr.begin() + i));
}

int drawText(int x, int y, const std::string &str, uint16_t fg = 0, uint16_t bg = 0, int len = -1) {
  auto wstr = string2wstring(str);
  int cnt = 0;
  uint32_t c;

  for (int i = 0; i < static_cast<int>(wstr.length() - 1); ++i) {
    char s[8] = {0};
    wctomb(s, wstr[i]);

    tb_utf8_char_to_unicode(&c, s);

    auto cw = tb_wcwidth(wstr[i]);
    if(len != -1 && cnt + cw > len) break;

    tb_change_cell(x + cnt, y, c, fg, bg);
    cnt += cw;
  }

  return cnt;
}

#endif
