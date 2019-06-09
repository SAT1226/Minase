#ifndef NANOSYNTAXHIGHLIGHT_HPP
#define NANOSYNTAXHIGHLIGHT_HPP

#include <iostream>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>

#include <regex.h>
#include <dirent.h>
#include <sys/types.h>

class NanoSyntaxHighlight {
  friend class NanoSyntaxHighlightTest;

public:
  NanoSyntaxHighlight() :tabsize_(-1), lineNumbers_(false) {}

  std::string highlight(const std::string& fileName, const std::string& txt) {

    fcolorBuf_.resize(txt.length());
    bcolorBuf_.resize(txt.length());
    std::fill(fcolorBuf_.begin(), fcolorBuf_.end(), 0);
    std::fill(bcolorBuf_.begin(), bcolorBuf_.end(), 0);

    int n = getHighlightType(fileName);
    if(n == -1) return txt;

    const std::vector<HighlightRule>& hlRuleList = hightlightList_[n].highlightRuleList;
    for(size_t i = 0; i < hlRuleList.size(); ++i) {
      if(hlRuleList[i].regex.front() == '"' && hlRuleList[i].regex.back() == '"') {
        auto rStr = hlRuleList[i].regex.substr(1, hlRuleList[i].regex.length() - 2);

        int fcolor = getColorCode(hlRuleList[i].fcolor);
        int bcolor = getColorCode(hlRuleList[i].bcolor) + 10;

        regexNormal(txt, rStr, fcolor, bcolor, hlRuleList[i].icase);
      }
      else if(stringStartWith(hlRuleList[i].regex, "start") && i + 1 < hlRuleList.size() &&
              stringStartWith(hlRuleList[i + 1].regex, "end")) {
        auto rStr1 = getSurroundDoubleQuotation(hlRuleList[i].regex);
        auto rStr2 = getSurroundDoubleQuotation(hlRuleList[i + 1].regex);

        int fcolor = getColorCode(hlRuleList[i].fcolor);
        int bcolor = getColorCode(hlRuleList[i].bcolor) + 10;

        regexSurround(txt, rStr1, rStr2, fcolor, bcolor,
                      hlRuleList[i].icase, hlRuleList[i + 1].icase);
      }
    }

    std::string result;
    int currentFColor = 0, currentBColor = 0, lineCnt = 0;
    for(size_t i = 0; i < txt.length(); ++i) {
      std::string colorStr;

      if(currentFColor != fcolorBuf_[i]) {
        currentFColor = fcolorBuf_[i];
        colorStr.append(getAnsiFColor(currentFColor));
      }
      if(currentBColor != bcolorBuf_[i]) {
        currentBColor = bcolorBuf_[i];
        colorStr.append(getAnsiBColor(currentBColor));
      }

      if(i != 0 && txt[i - 1] == '\n' && colorStr.empty()) {
        if(currentFColor != 0 || currentBColor != 0) {
          colorStr.append(getAnsiFColor(currentFColor));
          colorStr.append(getAnsiBColor(currentBColor));
        }
      }

      if(lineNumbers_ && (i == 0 || txt[i - 1] == '\n')) {
        char buf[80];
        snprintf(buf, sizeof(buf), "\e[39;49m%6d: ", ++lineCnt);

        colorStr = buf + colorStr;
      }

      if(tabsize_ >= 0) {
        if(txt[i] == '\t') result.append(colorStr + tab2space());
        else result.append(colorStr + txt[i]);
      }
      else result.append(colorStr + txt[i]);
    }

    return result;
  }

  std::string highlight(const std::string& fileName, FILE* fp) {
    std::string txt;
    char rbuf[1024];
    while(!feof(fp)) {
      if(fgets(rbuf, sizeof(rbuf), fp) != 0) {
        auto str = std::string(rbuf);

        // remove crlf
        if(str.back() == '\n') str.pop_back();
        if(str.back() == '\r') str.pop_back();
        str.push_back('\n');

        txt += str;
      }
    }

    return highlight(fileName, txt);
  }

  void setTabSpace(int tabsize) { tabsize_ = tabsize; }
  int getTabSpace() const { return tabsize_; }
  void setLineNumbers(bool t) { lineNumbers_ = t; }
  bool getLineNumbers() const { return lineNumbers_; }
  std::string getCurrentSyntaxName() const { return currentSyntaxName_; }

  bool loadPathNanoRC(const std::string& path) {
    auto dir = opendir(path.c_str());
    if(dir == NULL) return false;

    struct dirent* dp;
    while((dp = readdir(dir)) != NULL) {
      std::string name(dp -> d_name);

      auto i = name.find_last_of(".");
      if(i == 0) continue;
      if(i == std::string::npos) continue;

      auto suffix = name.substr(i + 1, name.size() - i);
      if(suffix == "nanorc") {
        if(path.back() != '/') name = '/' + name;

        loadNanoRC(path + name);
      }
    }
    closedir(dir);

    return true;
  }

  bool loadNanoRC(const std::string& fileName) {
    FILE* fp;
    if((fp = fopen(fileName.c_str(), "r")) == NULL) {
      return false;
    }

    char rbuf[5120];
    Highlight hightlight;

    while(!feof(fp)) {
      if(fgets(rbuf, sizeof(rbuf), fp) != 0) {
        if(rbuf[0] == '#') continue;
        if(rbuf[0] == '\n') continue;

        auto splitTxt = splitText(rbuf);
        if(splitTxt[0] == "syntax") {
          hightlight.name = getSurroundDoubleQuotation(splitTxt[1]);

          for(size_t i = 2; i < splitTxt.size(); ++i)
            hightlight.fileRegexList.emplace_back(splitTxt[i]);
        }
        else if(splitTxt[0] == "magic") {
          hightlight.magicRegex = splitTxt[1];
        }
        else if(splitTxt[0] == "comment") {
          hightlight.commentRegex = splitTxt[1];
        }
        else if(splitTxt[0] == "color" || splitTxt[0] == "icolor") {
          bool icase = (splitTxt[0] == "color") ? false: true;
          std::string fcolor, bcolor;

          auto i = splitTxt[1].find_first_of(',');
          if(i == std::string::npos)
            fcolor = splitTxt[1];
          else {
            fcolor = std::string(splitTxt[1].begin(), splitTxt[1].begin() + i);
            bcolor = std::string(splitTxt[1].begin() + i + 1, splitTxt[1].end());;
          }

          for(size_t i = 2; i < splitTxt.size(); ++i) {
            HighlightRule hlRule;

            hlRule.fcolor = fcolor;
            hlRule.bcolor = bcolor;
            hlRule.regex = splitTxt[i];
            hlRule.icase = icase;
            hightlight.highlightRuleList.emplace_back(hlRule);
          }
        }
      }
    }

    fclose(fp);
    hightlightList_.emplace_back(hightlight);

    return true;
  }

private:
  bool stringStartWith(const std::string& str, const std::string& start) {
    return strncmp(str.c_str(), start.c_str(), start.length()) == 0;
  }

  int getHighlightType(const std::string& fileName) {

    int n = -1;
    currentSyntaxName_ = "";
    for(size_t i = 0; i < hightlightList_.size(); ++i) {
      for(auto regex: hightlightList_[i].fileRegexList) {
        regex_t re;
        regmatch_t m[1];

        auto rstr = getSurroundDoubleQuotation(regex);
        regcomp(&re, rstr.c_str(),
                REG_EXTENDED|REG_NEWLINE|REG_NOSUB|REG_ICASE);

        if(regexec(&re, fileName.c_str(), 0, m, 0) != REG_NOMATCH) {
          n = i;
          regfree(&re);
          break;
        }
        regfree(&re);
      }
    }

    if(n != -1)
      currentSyntaxName_ = hightlightList_[n].name;

    return n;
  }

  int getColorCode(const std::string& colorName) const {
    if(colorName == "black") return 30;
    if(colorName == "red") return 31;
    if(colorName == "green") return 32;
    if(colorName == "yellow") return 33;
    if(colorName == "blue") return 34;
    if(colorName == "magenta") return 35;
    if(colorName == "cyan") return 36;
    if(colorName == "white") return 37;

    if(colorName == "brightblack") return 90;
    if(colorName == "brightred") return 91;
    if(colorName == "brightgreen") return 92;
    if(colorName == "brightyellow") return 93;
    if(colorName == "brightblue") return 94;
    if(colorName == "brightmagenta") return 95;
    if(colorName == "brightcyan") return 96;
    if(colorName == "brightwhite") return 97;

    return 39;
  }

  bool isBlankLine(const std::string& str) const {
    for(size_t i = 0; i < str.length(); ++i)
      if(str[i] != '\n') return false;

    return true;
  }

  std::string getAnsiFColor(int color) const {
    std::string result;

    if(color == 0) result ="\e[39m";
    else result = "\e[" + std::to_string(color) + "m";

    return result;
  }

  std::string getAnsiBColor(int color) const {
    std::string result;

    if(color == 0) result = "\e[49m";
    else result = "\e[" + std::to_string(color) + "m";

    return result;
  }

  void regexNormal(const std::string& txt, const std::string& reg,
                   int fcolor, int bcolor, bool icase) {
    int cflags = icase ? REG_EXTENDED|REG_NEWLINE|REG_ICASE : REG_EXTENDED|REG_NEWLINE;
    regex_t re;
    regmatch_t m[1];
    regcomp(&re, reg.c_str(), cflags);

    auto ptxt = txt.c_str();
    while(1) {
      int eflags = (ptxt == txt.c_str()) ? REG_NOTEOL : REG_NOTBOL|REG_NOTEOL;

      if(regexec(&re, ptxt, 1, m, eflags) != REG_NOMATCH) {
        if(m[0].rm_so != -1) {
          auto mStr = std::string(ptxt + m[0].rm_so, ptxt + m[0].rm_eo);

          if(!isBlankLine(mStr)) {
            int begin = ptxt + m[0].rm_so - txt.c_str();
            int end = ptxt + m[0].rm_eo - txt.c_str();

            for(int j = begin; j < end; ++j) {
              fcolorBuf_[j] = fcolor;
              bcolorBuf_[j] = bcolor;
            }
          }

          ptxt = ptxt + m[0].rm_eo;
          if(m[0].rm_eo == 0) ++ptxt;
          if(ptxt >= txt.c_str() + txt.length()) break;
        }
      }
      else break;
    }
    regfree(&re);
  }

  void regexSurround(const std::string& txt, const std::string& sreg, const std::string& ereg,
                     int fcolor, int bcolor, bool icase1, bool icase2) {
    regmatch_t m[1];
    regex_t re1, re2;

    int cflag1 = icase1 ? REG_EXTENDED|REG_ICASE : REG_EXTENDED;
    int cflag2 = icase2 ? REG_EXTENDED|REG_ICASE : REG_EXTENDED;

    if(sreg[0] != '^' && sreg.find_first_of('$') == std::string::npos)
      regcomp(&re1, sreg.c_str(), cflag1);
    else
      regcomp(&re1, sreg.c_str(), cflag1|REG_NEWLINE);

    if(ereg[0] != '^' && ereg.find_first_of('$') == std::string::npos)
      regcomp(&re2, ereg.c_str(), cflag2);
    else
      regcomp(&re2, ereg.c_str(), cflag2|REG_NEWLINE);

    int begin = 0, end = 0;
    const char* ptxt = txt.c_str();
    while(1) {
      if(regexec(&re1, ptxt, 1, m, 0) != REG_NOMATCH) {
        begin = ptxt + m[0].rm_so - txt.c_str();

        ptxt = ptxt + m[0].rm_eo;
        if(ptxt > txt.c_str() + txt.length()) break;

        if(regexec(&re2, ptxt, 1, m, 0) != REG_NOMATCH) {
          end = ptxt + m[0].rm_eo - txt.c_str();

          for(int j = begin; j < end; ++j) {
            fcolorBuf_[j] = fcolor;
            bcolorBuf_[j] = bcolor;
          }

          ptxt = ptxt + m[0].rm_eo;
          if(m[0].rm_eo == 0) ++ptxt;
          if(ptxt >= txt.c_str() + txt.length()) break;
        }
        else {
          end = txt.length();

          for(int j = begin; j < end; ++j) {
            fcolorBuf_[j] = fcolor;
            bcolorBuf_[j] = bcolor;
          }
          break;
        }
      }
      else break;
    }

    regfree(&re1);
    regfree(&re2);
  }

  std::string tab2space() const {
    std::string result;

    for(int i = 0; i < tabsize_; ++i)
      result.push_back(' ');

    return result;
  }

  std::string getSurroundDoubleQuotation(const std::string& str) const {
    auto begin = str.find_first_of('"');
    auto end = str.find_last_of('"');

    if(begin != std::string::npos && end != std::string::npos && begin != end)
      return str.substr(begin + 1, end - begin - 1);

    return str;
  }

  std::vector<std::string> splitText(const std::string& txt) const {
    std::vector<std::string> result;

    bool qmark = false;
    for(size_t i = 0; i < txt.length(); ++i) {
      if(txt[i] == ' ') continue;

      qmark = false;
      for(size_t j = i; j < txt.length(); ++j) {
        if(txt[j] == '"') {
          if((!qmark && (txt[j - 1] == ' ' || txt[j - 1] == '=')) ||
             (qmark && (txt[j + 1] == ' ' || txt[j + 1] == '\n'))) {

            if(qmark) qmark = false;
            else qmark = true;
          }
        }

        if(!qmark && (txt[j] == ' ' || txt[j] == '\n')) {
          result.emplace_back(std::string(&txt[i], &txt[j]));
          i = j;
          break;
        }
      }
    }

    return result;
  }

  struct HighlightRule {
    std::string fcolor;
    std::string bcolor;

    std::string regex;
    bool icase;
  };

  struct Highlight {
    std::string name;
    std::vector<std::string> fileRegexList;

    std::string magicRegex;
    std::string commentRegex;

    std::vector<HighlightRule> highlightRuleList;
  };
  std::vector<Highlight> hightlightList_;
  std::vector<int> fcolorBuf_, bcolorBuf_;
  std::string currentSyntaxName_;
  int tabsize_;
  bool lineNumbers_;
};

#endif
