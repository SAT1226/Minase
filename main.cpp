#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <atomic>
#include <deque>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <array>

#include <linux/fs.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <termios.h>
#include <dirent.h>
#include <locale.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <fcntl.h>
#include <langinfo.h>

#include <iconv.h>
#include <uchardet/uchardet.h>
#include <taglib/fileref.h>
#include <taglib/tdebuglistener.h>

#include "./termbox/termbox.h"
#include "./libbsd/strmode.h"
#include "./inih/INIReader.h"
#include "./tsl/robin_set.h"
#include "./cpp-linenoise/linenoise.hpp"
#include "./cmdline/cmdline.h"

#include "NanoSyntaxHighlight.hpp"
#include "ImageUtil.hpp"
#include "TermboxUtil.hpp"

const static int TAB_MAX = 4;
static const char* const TMP_FILENAME = "/tmp/minase_tmp";

class NullListener : public TagLib::DebugListener {
public:
  virtual void printMessage(const TagLib::String &msg) {(void)msg;}
};

static std::string getBaseName(const std::string& name)
{
  auto pos = name.find_last_of('/');

  if(pos != std::string::npos)
    return name.substr(name.find_last_of('/') + 1);

  return "";
}

static std::string getDirName(const std::string& name)
{
  auto pos = name.find_last_of('/');

  if(pos != std::string::npos)
    return name.substr(0, name.find_last_of('/'));

  return "";
}

static std::string getSuffix(const std::string &name)
{
  auto i = name.find_last_of(".");

  if(i == 0) return "";
  if(i == std::string::npos) return "";
  return name.substr(i + 1, name.size() - i);
}

#ifdef USE_MIGEMO
#include <migemo.h>
migemo* mgm;
static const char* DEFAULT_MIGEMO_DICT = "/usr/share/migemo/utf-8/migemo-dict";

/*
+https://github.com/koron/cmigemo
+  charset.c: utf8_int2char
+  rxgen.c: default_int2char
+
+  Copyright (c) 2003-2007 MURAOKA Taro (KoRoN)
+  Released under the MIT license
+  URL: https://github.com/koron/cmigemo/blob/master/doc/LICENSE_MIT.txt
+
+  UTF8 + PCRE用に改変
+*/
static int utf8pcre_int2char(unsigned int in, unsigned char* out)
{
  if(in < 0x80) {
    int len = 0;

    /* outは最低でも16バイトはある、という仮定を置く */
    switch(in) {
    case '\\':
    case '.': case '*': case '^': case '$': case '/':
    case ' ': case '(': case ')': case '+':
    case '-': case '?': case '[': case ']': case '{':
    case '|': case '}':
      if(out) out[len] = '\\';
      ++len;
    default:
      if(out) out[len] = (unsigned char)(in & 0xFF);
      ++len;
      break;
    }
    return len;
  }

  if(in < 0x800) {
    if(out) {
      out[0] = 0xc0 + (in >> 6);
      out[1] = 0x80 + ((in >> 0) & 0x3f);
    }
    return 2;
  }
  if(in < 0x10000) {
    if(out) {
      out[0] = 0xe0 + (in >> 12);
      out[1] = 0x80 + ((in >> 6) & 0x3f);
      out[2] = 0x80 + ((in >> 0) & 0x3f);
    }
    return 3;
  }
  if(in < 0x200000) {
    if(out) {
      out[0] = 0xf0 + (in >> 18);
      out[1] = 0x80 + ((in >> 12) & 0x3f);
      out[2] = 0x80 + ((in >> 6) & 0x3f);
      out[3] = 0x80 + ((in >> 0) & 0x3f);
    }
    return 4;
  }
  if(in < 0x4000000) {
    if(out) {
      out[0] = 0xf8 + (in >> 24);
      out[1] = 0x80 + ((in >> 18) & 0x3f);
      out[2] = 0x80 + ((in >> 12) & 0x3f);
      out[3] = 0x80 + ((in >> 6) & 0x3f);
      out[4] = 0x80 + ((in >> 0) & 0x3f);
    }
    return 5;
  }
  else {
    if(out) {
      out[0] = 0xf8 + (in >> 30);
      out[1] = 0x80 + ((in >> 24) & 0x3f);
      out[2] = 0x80 + ((in >> 18) & 0x3f);
      out[3] = 0x80 + ((in >> 12) & 0x3f);
      out[4] = 0x80 + ((in >> 6) & 0x3f);
      out[5] = 0x80 + ((in >> 0) & 0x3f);
    }
    return 6;
  }
}
/*-end-*/
#endif

class Config {
public:
  Config() : logMaxlines_(100), preViewMaxlines_(50), fileViewType_(0),
             sortType_(0), sortOrder_(0),
             useTrash_(false), wcwidthCJK_(false),
             nanorcPath_("/usr/share/nano"), opener_("xdg-open"),
             archiveMntDir_("~/.config/Minase/mnt")
  {}

  bool LoadConfig(const std::string& fileName) {
    INIReader reader(fileName);

    if(reader.ParseError() != 0) {
      std::cerr << "Can't load : " << fileName << std::endl;;
      return false;
    }

    logMaxlines_ = reader.GetInteger("Options", "LogMaxLines", 100);
    preViewMaxlines_ = reader.GetInteger("Options", "PreViewMaxLines", 50);
    useTrash_ = reader.GetBoolean("Options", "UseTrash", false);
    nanorcPath_ = reader.Get("Options", "NanorcPath", "/usr/share/nano");
    wcwidthCJK_ = reader.GetBoolean("Options", "wcwidth-cjk", false);
    opener_ = reader.Get("Options", "Opener", "xdg-open");
    fileViewType_ = reader.GetInteger("Options", "FileViewType", 0);
    sortType_ = reader.GetInteger("Options", "SortType", 0);
    sortOrder_ = reader.GetInteger("Options", "SortOrder", 0);
    filterType_ = reader.GetInteger("Options", "FilterType", 0);
    archiveMntDir_ = reader.Get("Options", "ArchiveMntDir", "~/.config/Minase/mnt");

#ifdef USE_MIGEMO
    migemoDict_ = reader.Get("Options", "MigemoDict", DEFAULT_MIGEMO_DICT);
#endif
    return true;
  }

  bool LoadBookmarks(const std::string& fileName) {
    FILE* fp;

    if((fp = fopen(fileName.c_str(), "r")) == NULL)
      return false;

    char buf[4096];
    while(!feof(fp)) {
      if(fgets(buf, sizeof(buf), fp) != 0) {
        std::string bookmark(buf);

        if(bookmark.back() == '\n') bookmark.pop_back();
        if(bookmark.back() == '\r') bookmark.pop_back();

        bookmarks_.emplace_back(bookmark);
      }
    }

    fclose(fp);
    return true;
  }

  struct Plugin {
    bool gui;
    std::string name;
    std::string filePath;
    std::string key;
    bool inputText;
    bool silent;

    enum Operation {
      NONE,
      CHANGE_DIRECTORY,
      CHANGE_CURRENT_FILE,
    };
    Operation operation;
  };

  bool LoadPlugins(const std::string& fileName) {
    INIReader reader(fileName);

    if(reader.ParseError() != 0) {
      std::cerr << "Can't load : " << fileName << std::endl;;
      return false;
    }

    auto sections = reader.Sections();
    for(auto const& section : sections) {
      Plugin p;

      p.name = section;
      p.filePath = reader.Get(section, "filePath", "");
      p.gui = reader.GetBoolean(section, "gui", false);
      p.key = reader.Get(section, "key", "");

      p.inputText = false;
      if(!p.filePath.empty()) {
        auto basename = getBaseName(p.filePath);
        if(!basename.empty()) {
          char op = basename[0];

          if(basename[0] == '_') {
            p.inputText = true;

            if(basename.length() > 2) op = basename[1];
          }
          p.silent = (basename.back() == '%');

          switch(op) {
          case '0':
            p.operation = Plugin::Operation::NONE;
            break;

          case '1':
            p.operation = Plugin::Operation::CHANGE_DIRECTORY;
            p.gui = false;
            break;

          case '2':
            p.operation = Plugin::Operation::CHANGE_CURRENT_FILE;
            p.gui = false;
            break;

          default:
            p.operation = Plugin::Operation::NONE;
          };
        }
      }

      plugins_.emplace_back(p);
    }

    return true;
  }

  int getLogMaxLines() const { return logMaxlines_; }
  int getPreViewMaxLines() const { return preViewMaxlines_; }
  int getFileViewType() const { return fileViewType_; }
  int getSortType() const { return sortType_; }
  int getSortOrder() const { return sortOrder_; }
  int getFilterType() const { return filterType_; }
  std::string getArchiveMntDir() const { return archiveMntDir_; }
#ifdef USE_MIGEMO
  std::string getMigemoDict() const { return migemoDict_; }
#endif
  bool useTrash() const { return useTrash_; }
  bool wcwidthCJK() const { return wcwidthCJK_; }
  std::string getNanorcPath() const { return nanorcPath_; }
  std::string getOpener() const { return opener_; }
  std::vector<std::string> getBookmarks() const { return bookmarks_; }
  std::vector<Plugin> getPlugins() const { return plugins_; }

private:
  int logMaxlines_;
  int preViewMaxlines_;
  int fileViewType_;
  int sortType_, sortOrder_;
  int filterType_;
  bool useTrash_;
  bool wcwidthCJK_;
  std::string nanorcPath_, opener_, archiveMntDir_;
  std::vector<std::string> bookmarks_;
  std::vector<Plugin> plugins_;

#ifdef USE_MIGEMO
  std::string migemoDict_;
#endif
};

Config config;

int spawn(const std::string& cmd, const std::string& args1,
          const std::string& args2, const std::string& args3,
          const std::string& dir, bool gui = false, bool silent = false)
{
  bool chDir = dir.empty() ? false : true;
  pid_t pid;

  if(!gui) {
    if(!silent) tb_shutdown();
    pid = fork();

    if(pid < 0) return -1;
    else if(pid == 0) {
      if(chDir) chdir(dir.c_str());
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      execlp(cmd.c_str(), cmd.c_str(),
             args1.empty() ? NULL : args1.c_str(),
             args2.empty() ? NULL : args2.c_str(),
             args3.empty() ? NULL : args3.c_str(),
             NULL);

      _exit(1);
    }

    int stat = 0;
    waitpid(pid, &stat, 0);
    if(WIFSIGNALED(stat)) printf("\n");

    if(!silent) tb_init();

    if(WIFEXITED(stat)) {
      return WEXITSTATUS(stat);
    }
  }
  else {
    pid = fork();

    if(pid < 0) return -1;
    else if(pid == 0) {
      pid_t pid2 = fork();

      if(pid2 < 0) _exit(1);
      else if(pid2 == 0) {
        if(chDir) chdir(dir.c_str());
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        setsid();

        int fd = open("/dev/null", O_WRONLY, 0200);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        execlp(cmd.c_str(), cmd.c_str(),
               args1.empty() ? NULL : args1.c_str(),
               args2.empty() ? NULL : args2.c_str(),
               args3.empty() ? NULL : args3.c_str(),
               NULL);

        _exit(1);
      }

      _exit(0);
    }

    int stat = 0;
    waitpid(pid, &stat, 0);
  }

  return 0;
}

pid_t popen2(const std::string& cmd,
             const std::vector<std::string>& args, int *infp, int *outfp, bool stderr_out = false)
{
  int p_stdin[2], p_stdout[2];
  pid_t pid;

  if(pipe(p_stdin) != 0)
    return -1;

  if(pipe(p_stdout) != 0) {
    close(p_stdin[0]);
    close(p_stdin[1]);

    return -1;
  }

  std::vector<char*> argc;
  argc.emplace_back(const_cast<char*>(cmd.c_str()));
  for(auto const& s : args)
    argc.emplace_back(const_cast<char*>(s.c_str()));
  argc.push_back(0);

  pid = fork();

  if(pid < 0) {
    close(p_stdin[0]);
    close(p_stdin[1]);
    close(p_stdout[0]);
    close(p_stdout[1]);

    return pid;
  }
  else if(pid == 0) {
    dup2(p_stdin[0], STDIN_FILENO);
    dup2(p_stdout[1], STDOUT_FILENO);

    if(stderr_out) dup2(p_stdout[1], STDERR_FILENO);
    else {
      int fd = open("/dev/null", O_WRONLY, 0200);

      dup2(fd, STDERR_FILENO);
      close(fd);
    }

    close(p_stdin[0]);
    close(p_stdin[1]);
    close(p_stdout[0]);
    close(p_stdout[1]);

    execvp(cmd.c_str(), argc.data());
    perror("execvp");
    _exit(1);
  }

  close(p_stdin[0]);
  close(p_stdout[1]);

  if(infp == NULL) close(p_stdin[1]);
  else *infp = p_stdin[1];

  if(outfp == NULL) close(p_stdout[0]);
  else *outfp = p_stdout[0];

  return pid;
}

int pclose2(pid_t pid)
{
  int stat;
  waitpid(pid, &stat, 0);

  return WEXITSTATUS(stat);
}

bool which(std::string cmd)
{
  auto shell = getenv("SHELL");
  if(shell == 0) return false;

  if(spawn(shell, "-c", "which " + cmd + " > /dev/null", "", "", false, true) == 0)
    return true;
  else return false;
}

class FileInfo {
public:
  FileInfo(const std::string& path, const std::string& fileName) :
    path_(path), name_(fileName) {

    if(!path.empty()) {
      lstat(std::string(path_ + name_).c_str(), &lstat_);

      if(S_ISDIR(lstat_.st_mode)) dir_ = true;
      else if(S_ISLNK(lstat_.st_mode)) {
        struct stat s;
        stat(std::string(path_ + name_).c_str(), &s);
        dir_ = S_ISDIR(s.st_mode);
      }
      else dir_ = false;

      if(isDir()) name_ += '/';
    }
  }

  std::string getFileName() const { return name_; }
  std::string getPath() const { return path_; }
  std::string getFilePath() const { return path_ + name_; }
  std::string getSuffix() const {
    if(isDir()) return "";

    return ::getSuffix(name_);
  }
  bool isDir() const { return dir_; }
  bool isLink() const { return S_ISLNK(lstat_.st_mode); }
  bool isFifo() const { return S_ISFIFO(lstat_.st_mode); }
  bool isSock() const { return S_ISSOCK(lstat_.st_mode); }

  bool isExe() const {
    if(S_ISREG(lstat_.st_mode))
      return lstat_.st_mode & S_IXUSR;
    return false;
  }
  mode_t getMode() const { return lstat_.st_mode; }
  off_t getSize() const { return lstat_.st_size; }
  timespec getMTime() const { return lstat_.st_mtim; }

  static std::string getModeStr(const FileInfo& fileInfo) {
    char strMode[80];
    strmode(fileInfo.getMode(), strMode);

    return strMode;
  }

  static std::string getMTimeStr(const FileInfo& fileInfo) {
    struct tm tm;
    auto mtim = fileInfo.getMTime();
    localtime_r(&mtim.tv_sec, &tm);

    char buf[256];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1,
            tm.tm_mday, tm.tm_hour,
            tm.tm_min, tm.tm_sec);

    return std::string(buf);
  }

  /*
   * https://github.com/jarun/nnn
   * nnn.c: char *coolsize(off_t size)
   *
   * BSD 2-Clause License
   *
   * Copyright (C) 2014-2016, Lazaros Koromilas <lostd@2f30.org>
   * Copyright (C) 2014-2016, Dimitris Papastamos <sin@2f30.org>
   * Copyright (C) 2016-2019, Arun Prakash Jana <engineerarun@gmail.com>
   * All rights reserved.
  */
  static std::string getSizeStr(const FileInfo& fileInfo) {
    auto size = fileInfo.getSize();
    static const char * const U = "BKMGTPEZY";
    static char size_buf[12]; /* Buffer to hold human readable size */
    static off_t rem;
    static int i;

    rem = i = 0;

    while (size > 1024) {
      rem = size & (0x3FF); /* 1024 - 1 = 0x3FF */
      size >>= 10;
      ++i;
    }

    if (i == 1) {
      rem = (rem * 1000) >> 10;

      rem /= 10;
      if (rem % 10 >= 5) {
        rem = (rem / 10) + 1;
        if (rem == 10) {
          ++size;
          rem = 0;
        }
      } else
        rem /= 10;
    } else if (i == 2) {
      rem = (rem * 1000) >> 10;

      if (rem % 10 >= 5) {
        rem = (rem / 10) + 1;
        if (rem == 100) {
          ++size;
          rem = 0;
        }
      } else
        rem /= 10;
    } else if (i > 0) {
      rem = (rem * 10000) >> 10;

      if (rem % 10 >= 5) {
        rem = (rem / 10) + 1;
        if (rem == 1000) {
          ++size;
          rem = 0;
        }
      } else
        rem /= 10;
    }

    if (i > 0 && i < 6)
      snprintf(size_buf, 12, "%lu.%0*lu%c", (ulong)size, i, (ulong)rem, U[i]);
    else
      snprintf(size_buf, 12, "%lu%c", (ulong)size, U[i]);

    return std::string(size_buf);
  }
  /*-end-*/

private:
  std::string path_;
  std::string name_;
  struct stat lstat_;
  bool dir_;
};

class DirInfo {
public:
  DirInfo(const std::string& path, std::atomic<bool>* kill = 0):
    hidden_(false), sortType_(SortType::NAME), sortOrder_(SortOrder::ASCENDING), filterType_(FilterType::NORMAL) {

    switch(config.getSortType()) {
      case 0:
        sortType_ = SortType::NAME;
        break;
      case 1:
        sortType_ = SortType::SIZE;
        break;
      case 2:
        sortType_ = SortType::DATE;
        break;
    };

    if(config.getSortOrder() == 0) sortOrder_ = SortOrder::ASCENDING;
    else sortOrder_ = SortOrder::DESCENDING;

    switch(config.getFilterType()) {
    case 0:
      filterType_ = FilterType::NORMAL;
      break;
    case 1:
      filterType_ = FilterType::REGEXP;
      break;
#ifdef USE_MIGEMO
    case 2:
      filterType_ = FilterType::MIGEMO;
      break;
#endif
    };

    chdir(path, kill);
  }

  bool chdir(const std::string& path, std::atomic<bool>* kill = 0) {
    if(path_ != path) filter_ = "";
    path_ = path;
    fileList_.clear();
    filteredFileList_.clear();

    auto dir = opendir(path.c_str());
    if(dir == NULL) return false;

    struct dirent* dp;
    while((dp = readdir(dir)) != NULL) {
      if((dp -> d_name[0] == '.' && (dp -> d_name[1] == 0 || (dp -> d_name[1] == '.' && dp -> d_name[2] == 0))))
        continue;
      if(kill != 0 && *kill==true) {
        closedir(dir);
        return false;
      }

      std::shared_ptr<FileInfo> fileInfo(new FileInfo(path_, dp -> d_name));
      fileList_.emplace_back(fileInfo);
    }
    closedir(dir);
    filteredFileList();

    return true;
  }

  void showHiddenFiles(bool flg) {
    if(hidden_ != flg) {
      hidden_ = flg;
      filteredFileList();
    }
  }

  bool isShowHiddenFiles() const { return hidden_; }
  int getCount() const { return filteredFileList_.size(); }
  FileInfo at(int index) const { return *filteredFileList_[index]; }

  enum SortType {
    NAME,
    SIZE,
    DATE,
  };

  enum SortOrder {
    ASCENDING,
    DESCENDING,
  };

  enum FilterType {
    NORMAL,
    REGEXP,
#ifdef USE_MIGEMO
    MIGEMO,
#endif
  };

  void sort(SortType type, SortOrder order) {
    if(sortType_ != type || sortOrder_ != order) {
      sortType_ = type;
      sortOrder_ = order;
      filteredFileList();
    }
  }
  SortType getSortType() const { return sortType_; }
  SortOrder getSortOrder() const { return sortOrder_; }

  void filter(const std::string& filter, FilterType type) {
    filter_ = filter;
    filterType_ = type;

    filteredFileList();
  }
  std::string getFilter() const {
    return filter_;
  }
  FilterType getFilterType() const {
    return filterType_;
  }

private:
  class Filter {
  public:
    Filter() {}
    virtual ~Filter() {}
    virtual bool isMatch(const std::string& fileName) { (void)fileName; return true; };
  };

  class NormalFilter : public Filter {
  public:
    NormalFilter(const std::string& filter) {
      std::stringstream ss{filter};
      std::string buf;
      while(std::getline(ss, buf, ' ')) {
        std::transform(buf.cbegin(), buf.cend(), buf.begin(), toupper);
        filters_.push_back(buf);
      }
    }
    ~NormalFilter() {}

    bool isMatch(const std::string& fileName) {
      std::string uFileName;
      uFileName.resize(fileName.size());

      std::transform(fileName.cbegin(), fileName.cend(), uFileName.begin(), toupper);
      for(auto filter: filters_) {
        if(uFileName.find(filter) == std::string::npos) return false;
      }
      return true;
    }

  private:
    std::vector<std::string> filters_;
  };

  class RegexpFilter : public Filter {
  public:
    RegexpFilter(const std::string& filter) : reg_(false) {
      if(regcomp(&re_, filter.c_str(),
              REG_EXTENDED|REG_NEWLINE|REG_NOSUB|REG_ICASE) == 0) reg_ = true;
    }
    ~RegexpFilter() { if(reg_) regfree(&re_); }

    bool isMatch(const std::string& fileName) {
      if(!reg_) return true;

      if(!(regexec(&re_, fileName.c_str(), 0, m_, 0) != REG_NOMATCH))
        return false;

      return true;
    }

  private:
    bool reg_;
    regex_t re_;
    regmatch_t m_[1];
  };

#ifdef USE_MIGEMO
  class MigemoFilter : public Filter {
  public:
    MigemoFilter(const std::string& filter) {
      std::vector<std::string> filters;
      std::stringstream ss{filter};
      std::string buf;

      while(std::getline(ss, buf, ' ')) {
        std::transform(buf.cbegin(), buf.cend(), buf.begin(), toupper);
        filters.push_back(buf);
      }

      for(auto s: filters) {
        unsigned char* migemo_re = migemo_query(mgm, (unsigned char*)s.c_str());
        regex_t re;

        regcomp(&re, (char*)migemo_re,
                REG_EXTENDED|REG_NEWLINE|REG_NOSUB|REG_ICASE);

        filtersRegex_.emplace_back(re);
      }
    }
    ~MigemoFilter() {
      for(auto re: filtersRegex_) regfree(&re);
    }

    bool isMatch(const std::string& fileName) {
      for(auto re: filtersRegex_) {
        if(!(regexec(&re, fileName.c_str(), 0, m_, 0) != REG_NOMATCH)) return false;
      }

      return true;
    }

  private:
    std::vector<regex_t> filtersRegex_;
    regmatch_t m_[1];
  };
#endif

  void filteredFileList() {
    filteredFileList_.clear();
    std::unique_ptr<Filter> filterFunc(new Filter);

    if(!filter_.empty()) {
      switch(filterType_) {
      case NORMAL:
        filterFunc.reset(new NormalFilter(filter_));
        break;
      case REGEXP:
        filterFunc.reset(new RegexpFilter(filter_));
        break;
#ifdef USE_MIGEMO
      case MIGEMO:
        filterFunc.reset(new MigemoFilter(filter_));
        break;
#endif
      };
    }

    for(auto&& file: fileList_) {
      if(!(filterFunc -> isMatch(file -> getFileName()))) continue;

      if(!hidden_) {
        if(file -> getFileName()[0] != '.')
          filteredFileList_.emplace_back(file);
      }
      else filteredFileList_.emplace_back(file);
    }

    sortList();
  }

  void sortList() {
    typedef std::shared_ptr<FileInfo> FileInfo_Ptr;
    std::function<bool(const FileInfo_Ptr&, const FileInfo_Ptr&)> func;

    switch(sortType_) {
    case SortType::NAME:
      func = [](const FileInfo_Ptr& a, const FileInfo_Ptr& b)
             { return a -> getFileName() < b -> getFileName(); };
      break;

    case SortType::SIZE:
      func = [](const FileInfo_Ptr& a, const FileInfo_Ptr& b) {
        if(a -> getSize() == b -> getSize())
          return a -> getFileName() < b -> getFileName();
        else
          return a -> getSize() < b -> getSize();
      };
      break;

    case SortType::DATE:
      func = [](const FileInfo_Ptr& a, const FileInfo_Ptr& b) {
               auto at = a -> getMTime();
               auto bt = b -> getMTime();

               if (at.tv_sec == bt.tv_sec) {
                 if (at.tv_nsec == bt.tv_nsec)
                   return a -> getFileName() < b -> getFileName();
                 else
                   return at.tv_nsec < bt.tv_nsec;
               }
               else
                 return at.tv_sec < bt.tv_sec;
             };
      break;
    };

    if(sortOrder_ == SortOrder::DESCENDING)
      func = std::bind(func, std::placeholders::_2, std::placeholders::_1);

    std::sort(filteredFileList_.begin(), filteredFileList_.end(),
              [func](const FileInfo_Ptr& a, const FileInfo_Ptr& b) {
                if(a -> isDir() && b -> isDir())
                  return func(a, b);
                else if(a -> isDir() && !b -> isDir())
                  return true;
                else if(!a -> isDir() && b -> isDir())
                  return false;
                else return func(a, b);;
              });

  }

  bool hidden_;
  std::string path_, filter_;
  std::vector<std::shared_ptr<FileInfo>> fileList_;
  std::vector<std::shared_ptr<FileInfo>> filteredFileList_;
  SortType sortType_;
  SortOrder sortOrder_;
  FilterType filterType_;
};

class CheckFileType {
public:
  static bool isImage(FILE* fp) {
    fseek(fp, 0L, SEEK_SET);
    if(ImageUtil::checkHeader(fp) != ImageUtil::IMG_TYPE::IMG_UNKNOWN) {
      fseek(fp, 0L, SEEK_SET);
      return true;
    }

    fseek(fp, 0L, SEEK_SET);
    return false;
  }

  static bool isPDF(FILE* fp) {
    fseek(fp, 0L, SEEK_SET);

    unsigned char header[4];
    fread(header, 1, sizeof(header), fp);
    fseek(fp, 0L, SEEK_SET);

    if(header[0] == '%' && header[1] == 'P' &&
       header[2] == 'D' && header[3] == 'F') {
      return true;
    }
    return false;
  }

  static bool isSixel(FILE* fp) {
    fseek(fp, 0L, SEEK_SET);

    unsigned char header[3];
    fread(header, 1, sizeof(header), fp);
    fseek(fp, 0L, SEEK_SET);

    if(header[0] == 0x1B && header[1] == 0x50) {
      return true;
    }
    return false;
  }

  static bool isAudio(const FileInfo& fileInfo) {
    auto suffix = fileInfo.getSuffix();
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), tolower);

    if(suffix == "mp3" || suffix == "mp4" || suffix == "flac" ||
       suffix == "wav" || suffix == "ogg" || suffix == "wv" ||
       suffix == "tta" || suffix == "aiff" || suffix == "asf")
      return true;

    return false;
  }

  static bool isArchive(FILE* fp) {
    fseek(fp, 0L, SEEK_SET);

    unsigned char header[280];
    fread(header, 1, sizeof(header), fp);
    fseek(fp, 0L, SEEK_SET);

    // .gz
    if(header[0] == 0x1F && header[1] == 0x8B && header[2] == 0x08) {
      return true;
    }
    // .bzip2
    if(header[0] == 0x42 && header[1] == 0x5A && header[2] == 0x68) {
      if(header[3] < '1' || header[3] > '9') return false;

      if((header[4] == 0x31 && header[5] == 0x41 && header[6] == 0x59 &&
         header[7] == 0x26 && header[8] == 0x53 && header[9] == 0x59) ||
         (header[4] == 0x17 && header[5] == 0x72 && header[6] == 0x45 &&
         header[7] == 0x38 && header[8] == 0x50 && header[9] == 0x90))
        return true;
    }
    // .xz
    if(header[0] == 0xFD && header[1] == 0x37 && header[2] == 0x7A &&
       header[3] == 0x58 && header[4] == 0x5A && header[5] == 0x00) {
      return true;
    }

    // .zip
    if(header[0] == 0x50 && header[1] == 0x4B && header[2] == 0x03 && header[3] == 0x04) {
      return true;
    }
    // .7z
    if(header[0] == 0x37 && header[1] == 0x7A && header[2] == 0xBC && header[3] == 0xAF &&
       header[4] == 0x27 && header[5] == 0x1C) {
      return true;
    }
    // .rar
    if(header[0] == 0x52 && header[1] == 0x61 && header[2] == 0x72 &&
       header[3] == 0x21 && header[4] == 0x1A && header[5] == 0x07 &&
       (header[6] == 0x00 || (header[6] == 0x01 && header[7] == 0x00))) {
      return true;
    }
    // .cab
    if(header[0] == 0x4D && header[1] == 0x53 && header[2] == 0x43 &&
       header[3] == 0x46 && header[4] == 0x00 && header[5] == 0x00 &&
       header[6] == 0x00 && header[7] == 0x00) {
      return true;
    }
    // .lzh
    if(header[2] == 0x2D && header[3] == 0x6C && header[4] == 0x68 && header[6] == 0x2D) {
      if((header[5] >= '0' && header[5] <= '7') || header[5] == 'd' || header[5] == 's')
      return true;
    }
    // tar
    if((header[257] == 0x75 && header[258] == 0x73 && header[259] == 0x74 &&
        header[260] == 0x61 && header[261] == 0x72 && header[262] == 0x20 &&
        header[263] == 0x20 && header[264] == 0x00) ||
       (header[257] == 0x75 && header[258] == 0x73 && header[259] == 0x74 &&
        header[260] == 0x61 && header[261] == 0x72 && header[262] == 0x00 &&
        header[263] == 0x30 && header[264] == 0x30)) {
      return true;
    }

    return false;
  }

  static bool isBinary(FILE* fp) {
    fseek(fp, 0L, SEEK_SET);
    if(fgetc(fp) == EOF) return true;

    fseek(fp, 0L, SEEK_SET);
    if(isPDF(fp)) return true;
    if(isSixel(fp)) return true;

    fseek(fp, 0L, SEEK_SET);
    int cnt = 0;
    while(feof(fp) == 0) {
      auto c = fgetc(fp);

      if(c == EOF) {
        fseek(fp, 0L, SEEK_SET);
        return false;
      }
      if(c <= 0x08) {
        fseek(fp, 0L, SEEK_SET);
        return true;
      }
      if(++cnt > 512) break;
    }

    cnt = 0;
    fseek(fp, -512L, SEEK_END);
    while(feof(fp) == 0) {
      auto c = fgetc(fp);

      if(c == EOF) {
        break;
      }
      if(c <= 0x08) {
        fseek(fp, 0L, SEEK_SET);
        return true;
      }
      if(++cnt > 512) break;
    }

    fseek(fp, 0L, SEEK_SET);
    return false;
  }
};

class PreView {
public:
  PreView(const FileInfo& fileInfo) :
    kill_(false), done_(true), pid_(0), disable_(false), drawLoading_(false),
    imagePreview_(true), x_(0), y_(0), width_(0), height_(0), scroll_(0),
    fileInfo_(fileInfo) {

    highlight_.loadPathNanoRC(config.getNanorcPath());
    reload();
  }

  ~PreView() {
    if(thread_.joinable())
      thread_.detach();
  }

  bool isDisable() const {
    return disable_;
  }

  void setDisable(bool v) {
    disable_ = v;
  }

  void setImagePreview(bool v) {
    imagePreview_ = v;
  }

  bool isImagePreview() const {
    return imagePreview_;
  }

  std::string getLoadFileName() const {
    return fileInfo_.getFileName();
  }

  void cancel() {
    if(!done_) {
      while(!done_) {
        int pid = pid_;
        if(pid > 0) {
          kill(pid, SIGKILL);
        }
        kill_ = true;
      }
    }

    if(thread_.joinable()) thread_.join();
    kill_ = done_ = false;
    loadStartClock_ = std::chrono::system_clock::now();
    drawLoading_ = false;
    scroll_ = 0;

    implData_.clear();
  }

  bool scrollDown() {
    if(done_ && !implData_.getSixelNL()) {
      if(static_cast<int>(implData_.getTextRefNL().size()) >
         scroll_ + tb_height() - 3){
        ++scroll_;
        return true;
      }
    }
    return false;
  }

  bool scrollUp() {
    if(done_) {
      if(--scroll_ < 0) scroll_ = 0;
      else return true;
    }
    return false;
  }

  void reload() {
    cancel();
    thread_ = std::thread(&PreView::impl, this, fileInfo_);
  }

  void setLoadFile(const FileInfo& fileInfo) {
    fileInfo_ = fileInfo;
    reload();
  }

  void join() {
    if(thread_.joinable()) thread_.join();
  }

  std::string removeLF(const std::string &src) const {
    std::string dest = src;

    if(dest.back() == '\n') {
      dest.pop_back();
    }
    if(dest.back() == '\r') {
      dest.pop_back();
    }

    return dest;
  }

  std::string tab2Space(const std::string &src) const {
    std::string dest;

    for(int i = 0; i < static_cast<int>(src.length()); ++i) {
      if(src[i] == 0x09) dest += "  ";
      else dest += src[i];
    }

    return dest;
  }

  bool draw() {
    if(isDisable()) {
      clear();
      printf("\e[%d;%dH\e[7;1m%s\e[27;22m\e[K", y_ + 1, x_ + 1, "empty");
      fflush(stdout);
      return true;
    }

    if(!isLoading()) {
      fflush(stdout);
      implData_.lock();

      // sixel image
      if(implData_.getSixelNL()) {
        clear();
        printf("\e[%d;%dH", y_ + 1, x_ + 1);
        fflush(stdout);

        for(auto s: implData_.getTextRefNL()) {
          printf("%s", s.c_str());
        }
      }
      else {
        auto size = implData_.getTextRefNL().size();

        for(int i = y_; i < height_ + 1; ++i) {
          printf("\e[%d;%dH", i + 1, x_ + 1);
          fflush(stdout);
          if(size == 0 && i == y_) {
            printf("\e[7;1m%s\e[27;22m\e[K", "empty");
            continue;
          }
          if(size > static_cast<size_t>(scroll_ + i - y_)) {
            printf("%s\e[m\e[K",
                   strimwidth(tab2Space(implData_.getTextRefNL()[scroll_ + i - y_]), width_).c_str());
          }
          else {
            printf("\e[K");
          }
          fflush(stdout);
        }
      }
      implData_.unlock();
      fflush(stdout);
      return true;
    }
    else {
      if(!drawLoading_) {
        auto clock = std::chrono::system_clock::now();
        auto msec = std::chrono::duration_cast<
          std::chrono::milliseconds>(clock - loadStartClock_).count();

        if(msec >= 200) {
          fflush(stdout);
          clear();
          printf("\e[%d;%dHLoading...", y_ + 1, x_ + 1);
          fflush(stdout);
          drawLoading_ = true;
        }
      }

      return false;
    }
  }

  void clear() const {
    for(auto i = y_; i < height_ + 1; ++i) {
      printf("\e[%d;%dH", i + 1, x_ + 1);
      printf("\e[K");
      fflush(stdout);
    }
  }

  void setPosition(int x, int y) {
    x_ = x;
    y_ = y;
  }

  void setSize(int width, int height) {
    width_ = width;
    height_ = height;

    if(implData_.getSixel()) {
      implData_.clear();
    }
  }

  int getWidth() const { return width_; }
  int getHeight() const { return height_; }

  bool isSixel() {
    return implData_.getSixel();
  }

  bool isLoading() const {
    return !done_;
  }

private:
  void impl(const FileInfo& fileInfo) {
    std::vector<std::string> textBuf;
    bool sixel = false;

    if(fileInfo.isDir()) {
      textBuf = getPreviewDir(fileInfo);
    }
    else if(fileInfo.isFifo()) {
      textBuf.push_back("\e[7;1mfifo\e[27;22m");
    }
    else if(fileInfo.isSock()) {
      textBuf.push_back("\e[7;1msock\e[27;22m");
    }
    else {
      FILE* fp;
      if((fp = fopen(fileInfo.getFilePath().c_str(), "rb")) == NULL) {
        implData_.update(fileInfo.getFileName(), textBuf, sixel);
        done_ = true;

        return;
      }

      if(imagePreview_ && CheckFileType::isImage(fp)) {
        textBuf = getPreviewImage(fp, fileInfo);
        fclose(fp);

        sixel = true;
      }
      else if(CheckFileType::isAudio(fileInfo)) {
        fclose(fp);
        textBuf = getPreviewAudioTag(fileInfo);
      }
      else if(CheckFileType::isArchive(fp)) {
        fclose(fp);
        textBuf = getPreviewArchive(fileInfo);
      }
      else if(!CheckFileType::isBinary(fp)) {
        fclose(fp);
        textBuf = getPreviewText(fileInfo);
      }
      else {
        fclose(fp);
        textBuf.push_back("\e[7;1mbinary\e[27;22m");
      }
    }

    implData_.update(fileInfo.getFileName(), textBuf, sixel);
    done_ = true;
  }

  std::vector<std::string> getPreviewArchive(const FileInfo& fileInfo) {
    std::vector<std::string> result;
    std::vector<std::string> args{fileInfo.getFilePath()};

    getProcessText("lsar", args, result);
    if(result.empty()) {
      std::vector<std::string> args{"-tf",
                                    fileInfo.getFilePath()};
      getProcessText("bsdtar", args, result);
    }
    if(result.empty()) result.emplace_back("\e[7;1mbinary\e[27;22m");

    return result;
  }

  std::vector<std::string> getPreviewAudioTag(const FileInfo& fileInfo) {
    std::vector<std::string> result;
    TagLib::FileRef f(fileInfo.getFilePath().c_str());
    if(f.audioProperties() == nullptr) {
      result.emplace_back("No audio properties");
      return result;
    }

    int sec = f.audioProperties() -> length();
    int min = sec / 60;
    int s = sec % 60;

    char buf[80];
    snprintf(buf, sizeof(buf), "%02d:%02d", min, s);
    result.emplace_back("Length    : " + std::string(buf));
    result.emplace_back("\n");
    result.emplace_back("SampleRate: " + std::to_string(f.audioProperties() -> sampleRate()) + "hz");
    result.emplace_back("Bitrate   : " + std::to_string(f.audioProperties() -> bitrate()) + "kb/s");
    result.emplace_back("\n");
    result.emplace_back("Title     : " + f.tag() -> title().to8Bit(true));
    result.emplace_back("Artist    : " + f.tag() -> artist().to8Bit(true));
    result.emplace_back("Album     : " + f.tag() -> album().to8Bit(true));
    result.emplace_back("Comment   : " + f.tag() -> comment().to8Bit(true));
    result.emplace_back("Genre     : " + f.tag() -> genre().to8Bit(true));
    result.emplace_back("Year      : " + std::to_string(f.tag() -> year()));
    result.emplace_back("Track     : " + std::to_string(f.tag() -> track()));
    return result;
  }

  std::vector<std::string> getPreviewDir(const FileInfo& fileInfo) {
    std::vector<std::string> result;

    DirInfo dir(fileInfo.getFilePath(), &kill_);
    int maxCount;

    if(config.getPreViewMaxLines() != -1)
      maxCount = dir.getCount() > config.getPreViewMaxLines() ?
        config.getPreViewMaxLines() : dir.getCount();
    else
      maxCount = dir.getCount();

    for(int i = 0; i < maxCount; ++i) {
      auto f = dir.at(i);
      if(f.isDir()) {
        if(f.isLink())
          result.emplace_back("\e[36;1m" + f.getFileName());
        else
          result.emplace_back("\e[34;1m" + f.getFileName());
      }
      else if(f.isExe()) {
        result.emplace_back("\e[32;1m" + f.getFileName());
      }
      else if(f.isFifo()) {
        result.emplace_back("\e[33m" + f.getFileName());
      }
      else if(f.isSock()) {
        result.emplace_back("\e[35;1m" + f.getFileName());
      }
      else if(f.isLink()) {
        result.emplace_back("\e[36;1m" + f.getFileName());
      }
      else {
        result.emplace_back(f.getFileName());
      }

      if(kill_) return result;
    }

    return result;
  }

  std::vector<std::string> getPreviewImage(FILE*fp, const FileInfo& fileInfo) {
    std::vector<std::string> result;
    int col, row, xpixel, ypixel;
    getTermSize(&col, &row, &xpixel, &ypixel);

    int w = 0, h = 0;
    auto header = ImageUtil::checkHeader(fp);
    if(header == ImageUtil::IMG_TGA) {
      auto suffix = fileInfo.getSuffix();
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), tolower);

      if(suffix != "tga") {
        result.push_back("\e[7;1mbinary\e[27;22m");
        return result;
      }
    }

    if(!getSize(fp, header, w, h)) {
      return result;
    }

    int cw = xpixel / col, ch = ypixel / row;
    int scaleW = xpixel - (x_ * cw) - (cw * 2);
    int scaleH = ypixel - (y_ * ch) - (ch * 3);
    int sw = w, sh = h;

    if(!(w < scaleW && h < scaleH)) {
      ImageUtil::CalcScaleSize_KeepAspectRatio(w, h, scaleW, scaleH, sw, sh);
    }

    if(kill_) return result;
    std::vector<std::string> args{"-S",
                                  "-w" + std::to_string(sw), "-h" + std::to_string(sh),
                                  fileInfo.getFilePath()};
    getProcessText("img2sixel", args, result);

    return result;
  }

  std::string detectCharset(const std::string& txt) const {
    uchardet_t ucd = uchardet_new();

    if(uchardet_handle_data(ucd, txt.c_str(), txt.size()) != 0) {
      return "";
    }
    uchardet_data_end(ucd);

    auto charset = uchardet_get_charset(ucd);
    if(charset == 0) {
      uchardet_delete(ucd);
      return "";
    }
    else {
      std::string result(charset);
      uchardet_delete(ucd);

      return result;
    }
  }

  std::vector<std::string> getPreviewText(const FileInfo& fileInfo) {
    std::vector<std::string> ret;

    FILE* fp;
    if((fp = fopen(fileInfo.getFilePath().c_str(), "r")) == NULL) {
      return ret;
    }

    int cnt = 0;
    char rbuf[512];

    std::string txt;
    while(!feof(fp)) {
      if(fgets(rbuf, sizeof(rbuf), fp) != 0) {
        txt += rbuf;

        if(kill_) {
          fclose(fp);
          return ret;
        }

        // Remove CRLF
        if(txt.back() == '\n') txt.pop_back();
        else if(!feof(fp)) continue;
        if(txt.back() == '\r') txt.pop_back();

        txt.push_back('\n');
      }

      if(config.getPreViewMaxLines() == -1) ++cnt;
      else if(++cnt > config.getPreViewMaxLines()) break;

      if(kill_) {
        fclose(fp);
        return ret;
      }
    }
    fclose(fp);

    auto charset = detectCharset(txt);
    if(!charset.empty()) {
      if(!(charset == "ASCII" || charset == nl_langinfo(CODESET))) {
        size_t srclen = txt.length() + 1;
        size_t dstlen = txt.length() * 3 + 1;

        char* src = new char[srclen];
        char* dst = new char[dstlen];
        char* pdst = dst;
        char* psrc = src;

        memcpy(src, txt.c_str(), txt.length() + 1);

        std::string toCharset = std::string(nl_langinfo(CODESET)) + "//IGNORE";
        auto iv = iconv_open(toCharset.c_str(), charset.c_str());
        iconv(iv, &psrc, &srclen, &pdst, &dstlen);
        txt = dst;

        delete[] src;
        delete[] dst;
        iconv_close(iv);
      }
    }
    if(kill_) return ret;

    txt = highlight_.highlight(fileInfo.getFileName(), txt, std::ref(kill_));
    auto syntaxName = highlight_.getCurrentSyntaxName();
    if(syntaxName.empty()) syntaxName = "PlainText";

    ret.push_back("[Charset: " + charset + "] - " + syntaxName);

    std::string buf;
    std::stringstream ss{txt};
    while(std::getline(ss, buf)){
      ret.push_back(buf);
    }

    return ret;
  }

  bool getProcessText(const std::string& cmd,
                      const std::vector<std::string>& args,
                      std::vector<std::string>& buf, int maxline = 0) {
    int pipefd = 1;

    int pid = popen2(cmd, args, 0, &pipefd);
    if(pid < 0) {
      perror("can not exec command");
      return false;
    }
    pid_ = pid;

    FILE* fp;
    if((fp = fdopen(pipefd, "r")) == NULL) {
      perror("fdopen");

      close(pipefd);
      pclose2(pid_);
      pid_ = 0;

      return false;
    }

    char rbuf[4096];
    int cnt = 0;
    while(!feof(fp)) {

      if(fgets(rbuf, sizeof(rbuf), fp) != 0)
        buf.emplace_back(rbuf);

      if(kill_) break;
      if(maxline != 0 && ++cnt > maxline) break;
    }

    fclose(fp);
    close(pipefd);
    pclose2(pid_);
    pid_ = 0;

    return true;
  }

  void getTermSize(int* col, int* row, int* xpixel, int* ypixel) const {
    struct winsize ws;

    // get terminal size
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
      *col = ws.ws_col; // width
      *row = ws.ws_row; // height
      *xpixel = ws.ws_xpixel;
      *ypixel = ws.ws_ypixel;

      return;
    }

    *col = *row = *xpixel = *ypixel = -1;
  }

  class PreViewData {
  public:
    PreViewData() {}
    ~PreViewData() {}

    std::string getFileName() {
      std::lock_guard<std::mutex> lock(mutex_);
      return data_.fileName;
    }

    std::vector<std::string> getText() {
      std::lock_guard<std::mutex> lock(mutex_);
      return data_.text;
    }

    std::vector<std::string>& getTextRefNL() {
      return data_.text;
    }

    bool getSixelNL() const {
      return data_.sixel;
    }

    const std::string& getFileNameNL() {
      return data_.fileName;
    }

    bool getSixel() {
      std::lock_guard<std::mutex> lock(mutex_);
      return data_.sixel;
    }

    void update(const std::string& filename,
                std::vector<std::string>& text,
                bool sixel) {
      std::lock_guard<std::mutex> lock(mutex_);
      data_.fileName = filename;
      data_.text.swap(text);
      data_.sixel = sixel;
    }

    void clear() {
      std::lock_guard<std::mutex> lock(mutex_);
      data_.fileName = "";
      data_.text.clear();
      data_.sixel = false;
    }

    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }

  private:
    struct implData {
      std::string fileName;
      std::vector<std::string> text;
      bool sixel;
    };

    implData data_;
    std::mutex mutex_;
  };

  std::atomic<bool> kill_, done_;
  std::atomic<int> pid_;

  std::thread thread_;

  bool disable_, drawLoading_, imagePreview_;
  int x_, y_, width_, height_, scroll_;
  std::chrono::system_clock::time_point loadStartClock_;

  FileInfo fileInfo_;
  PreViewData implData_;
  NanoSyntaxHighlight highlight_;
};

class FileView {
public:
  FileView(const std::string& path) :
    dir_(path), path_(path), lastPath_(path), x_(0), y_(0),
    width_(20), height_(25), cursorPos_(0), oldScrollTop_(0),
    scroll_(false), viewType_(ViewType::SIMPLE) {

    if(config.getFileViewType() == 0) viewType_ = ViewType::SIMPLE;
    if(config.getFileViewType() == 1) viewType_ = ViewType::DETAIL;
  }

  std::string getPath() const { return path_; }
  bool setPath(const std::string& path) {
    auto dir = opendir(path.c_str());
    if(dir == NULL) return false;
    closedir(dir);

    lastPath_ = path_;
    path_ = path;
    update();
    setCursorPos(0);

    return true;
  }

  enum ViewType {
    SIMPLE,
    DETAIL,
  };

  ViewType getViewType() const {
    return viewType_;
  }
  void setViewType(ViewType type) {
    viewType_ = type;
  }

  void setPosition(int x, int y) {
    x_ = x;
    y_ = y;
  }

  void setSize(int width, int height) {
    width_ = width;
    height_ = height;
    scroll_ = true;
  }

  int getWidth() const { return width_; }
  int getHeight() const { return height_; }

  void showHiddenFiles(bool flg) {
    if(flg != dir_.isShowHiddenFiles()) {
      auto currentFileName = getCurrentFileName();
      dir_.showHiddenFiles(flg);

      int pos = searchFileName(currentFileName);
      if(pos != -1) setCursorPos(pos);
      else setCursorPos(0);
    }
  }

  bool isShowHiddenFiles() const {
    return dir_.isShowHiddenFiles();
  }

  int searchFileName(const std::string& fileName) {
    for(int i = 0; i < dir_.getCount(); ++i)
      if(dir_.at(i).getFileName() == fileName) return i;

    return -1;
  }

  std::string getLastPath() const { return lastPath_; }
  int getFileListCount() const { return dir_.getCount(); }
  bool isFileListEmpty() const { return dir_.getCount() == 0; }
  std::string getCurrentFileName() const { return dir_.at(cursorPos_).getFileName(); }
  std::string getCurrentFilePath() const { return dir_.at(cursorPos_).getFilePath(); }
  FileInfo getCurrentFileInfo() const { return dir_.at(cursorPos_); }
  FileInfo getFileInfo(int i) const { return dir_.at(i); }
  int getCursorPos() const { return cursorPos_; }
  void setCursorPos(int pos) {
    cursorPos_ = pos;
    scroll_ = true;
  }

  void sort(DirInfo::SortType type, DirInfo::SortOrder order) {
    if(dir_.getSortType() != type || dir_.getSortOrder() != order) {
      auto currentFileName = getCurrentFileName();
      dir_.sort(type, order);

      int pos = searchFileName(currentFileName);
      if(pos != -1) setCursorPos(pos);
      else setCursorPos(0);
    }
  }

  void filter(const std::string& filter, DirInfo::FilterType type) {
    dir_.filter(filter, type);
  }

  std::string getFilter() const {
    return dir_.getFilter();
  }

  DirInfo::FilterType getFilterType() const {
    return dir_.getFilterType();
  }

  bool update() {
    return dir_.chdir(path_);
  }

  bool isSelectedFile(const FileInfo& fileInfo) const {
    return selectedFiles_.find(fileInfo.getFilePath()) != selectedFiles_.end();
  }

  void selectFile(const FileInfo& fileInfo) {
    if(!isSelectedFile(fileInfo))
      selectedFiles_.insert(fileInfo.getFilePath());
  }

  void unselectFile(const FileInfo& fileInfo) {
    if(isSelectedFile(fileInfo)) {
      selectedFiles_.erase(fileInfo.getFilePath());
    }
  }

  std::vector<std::string> getSelectFiles() const {
    std::vector<std::string> result;

    for(auto filepath: selectedFiles_)
      result.emplace_back(filepath);

    return result;
  }

  void clearSelectFiles() {
    selectedFiles_.clear();
  }

  bool cursorPgDn() {
    if(getFileListCount() == 0) return false;

    int scroll = height_ / 2;
    if(cursorPos_ + scroll < getFileListCount() - 1) {
      cursorPos_ = cursorPos_ + scroll;
      scroll_ = true;
      return true;
    }
    else if(cursorPos_ != getFileListCount() - 1) {
      cursorPos_ = getFileListCount() - 1;
      scroll_ = true;
      return true;
    }

    return false;
  }

  bool cursorPgUp() {
    int scroll = height_ / 2;
    if(cursorPos_ - scroll > 0) {
      cursorPos_ = cursorPos_ - scroll;
      scroll_ = true;
      return true;
    }
    else if(cursorPos_ != 0) {
      cursorPos_ = 0;
      scroll_ = true;
      return true;
    }

    return false;
  }

  void cursorMoveTopOfScreen() {
    if(dir_.getCount() == 0) return;

    cursorPos_ = oldScrollTop_;
    scroll_ = false;
  }

  void cursorMoveMiddleOfScreen() {
    if(dir_.getCount() == 0) return;

    cursorPos_ = oldScrollTop_ + (height_ / 2) - 1;
    if(oldScrollTop_ + height_ > dir_.getCount())
      cursorPos_ = dir_.getCount() / 2 - 1;

    if(cursorPos_ < 0) cursorPos_ = 0;

    scroll_ = false;
  }

  void cursorMoveBottomOfScreen() {
    if(dir_.getCount() == 0) return;

    cursorPos_ = oldScrollTop_ + height_ - 1;
    if(oldScrollTop_ + height_ > dir_.getCount())
      cursorPos_ = dir_.getCount() - 1;

    scroll_ = false;
  }

  bool cursorNext() {
    if(cursorPos_ < getFileListCount() - 1) {
      ++cursorPos_;
      scroll_ = false;
      return true;
    }

    return false;
  }

  bool cursorPrev() {
    if(cursorPos_ > 0) {
      --cursorPos_;
      scroll_ = false;
      return true;
    }

    return false;
  }

  bool upDir() {
    auto oldPath = path_;
    auto i = path_.find_last_of('/', path_.length() - 2);
    if(i != std::string::npos) {
      auto newPath = path_.substr(0, i) + "/";
      if(setPath(newPath)) {
        int pos = searchFileName(oldPath.substr(i + 1, oldPath.length()));
        if(pos == -1) pos = 0;
        setCursorPos(pos);

        return true;
      }
      else {
        if(setPath(findUpDirName(newPath))) return true;
      }
    }

    return false;
  }

  void reload() {
    if(getFileListCount() != 0) {
      auto fileName = getCurrentFileName();
      int oldPos = searchFileName(fileName);
      int h = oldPos - oldScrollTop_;

      setPath(getPath());

      int pos = searchFileName(fileName);
      if(pos == -1 || oldPos == -1) {
        pos = 0;
        setCursorPos(pos);
      }
      else {
        oldScrollTop_ = pos - h;
        if(oldScrollTop_ < 0) {
          oldScrollTop_ = 0;
        }
        else scroll_ = false;
        cursorPos_ = pos;
      }
    }
    else {
      setPath(getPath());
      setCursorPos(0);
    }
  }

  void draw() {
    int scrollTop = 0;

    if(cursorPos_ > height_ / 2) {
      if(cursorPos_ + height_ / 2 < dir_.getCount()) {
        scrollTop = cursorPos_ - height_ / 2;
      }
      else {
        scrollTop = dir_.getCount() - height_;
        if(scrollTop < 0) scrollTop = 0;
      }
    }
    else scrollTop = 0;

    if(!scroll_) {
      if(oldScrollTop_ + height_ - y_ < cursorPos_) ++oldScrollTop_;
      else if(oldScrollTop_ > cursorPos_) --oldScrollTop_;

      if(height_ > dir_.getCount() - 1) {
        oldScrollTop_ = 0;
      }
      scrollTop = oldScrollTop_;
    }
    oldScrollTop_ = scrollTop;

    for(auto i = 0; i < height_; ++i) {
      if(dir_.getCount() == 0) {
        drawText(x_ + 1, y_ + i, "empty", TB_REVERSE);
        break;
      }
      if(i + scrollTop > dir_.getCount() - 1) break;

      int color = 0;
      auto fileInfo = dir_.at(i + scrollTop);

      if(isSelectedFile(fileInfo))
        drawText(x_, y_ + i, " ", 0, TB_MAGENTA);

      if(fileInfo.isDir())
        color = TB_BLUE | TB_BOLD;

      if(fileInfo.isExe())
        color = TB_GREEN | TB_BOLD;

      if(fileInfo.isFifo())
        color = TB_YELLOW;

      if(fileInfo.isSock())
        color = TB_MAGENTA | TB_BOLD;

      if(fileInfo.isLink())
        color = TB_CYAN | TB_BOLD;

      if(i + scrollTop == cursorPos_)
        color = color | TB_REVERSE;

      if(viewType_ == ViewType::SIMPLE) {
        drawText(x_ + 1, y_ + i, strimFileName(fileInfo, width_), color, 0);
      }
      else {
        char info[256];
        snprintf(info, sizeof(info), " %8.8s  %s",
                 FileInfo::getSizeStr(fileInfo).c_str(),
                 FileInfo::getMTimeStr(fileInfo).c_str());

        int infolen = strlen(info);
        int len = 0;
        std::string txt = strimFileName(fileInfo, width_ - infolen, &len);
        txt.pop_back();

        if(width_ - infolen - len > 0) {
          for(int i = 0; i < static_cast<int>(width_ - infolen) - len; ++i)
            txt.push_back(' ');
        }

        txt += info;
        drawText(x_ + 1, y_ + i, txt, color, 0, width_);
      }
    }
  }

  std::string strimFileName(const FileInfo& fileInfo, int w, int* len = 0) const {
    int llen;
    auto result = strimwidth(fileInfo.getFileName(), w, &llen);

    if(result.length() - 1 < fileInfo.getFileName().length()) {
      auto suffix = fileInfo.getSuffix();
      if(suffix.empty() || (llen - (suffix.length() + 2) < 0)) {
        if(len != 0) *len = llen;
        return result;
      }

      int len2;
      result = strimwidth(result, llen - (suffix.length() + 2), &len2);
      result.pop_back();

      result += "~." + suffix + '\0';
      llen = len2 + (suffix.length() + 2);
    }

    if(len != 0) *len = llen;
    return result;
  }

private:
  std::string findUpDirName(const std::string& path) {
    auto dir = opendir(path.c_str());
    if(dir == NULL) {
      if(path == "/") return "/";

      auto i = path.find_last_of('/', path.length() - 2);
      if(i != std::string::npos) {
        return findUpDirName(path.substr(0, i) + "/");
      }
      else return "/";
    }
    closedir(dir);

    return path;
  }

  DirInfo dir_;
  std::string path_, lastPath_;
  static tsl::robin_set<std::string> selectedFiles_;
  int x_, y_;
  int width_, height_, cursorPos_;
  int oldScrollTop_;
  bool scroll_;
  ViewType viewType_;
};

tsl::robin_set<std::string> FileView::selectedFiles_;

class FileOperation {
public:
  FileOperation() {
    operation_ = false;
    logTextUpDate_ = false;
    taskCnt_ = 0;
    kill_ = false;
  }

  ~FileOperation() {
    kill_ = true;
    if(thread_.joinable()) {
      thread_.join();
    }
  }

  void reloadPath(const std::string& path) {
    Task task;

    task.operation = Task::RELOAD;
    task.src = path;
    task.dst = "";

    addTask(task);
  }

  void addLogMessage(const std::string& txt) {
    Task task;

    task.operation = Task::ADD_LOGTEXT;
    task.src = txt;
    task.dst = "";

    addTask(task);
  }

  void copyFile(const std::string& src, const std::string& dst) {
    Task task;

    task.operation = Task::FILE_COPY;
    task.src = src;
    task.dst = dst;

    ++taskCnt_;
    addTask(task);
  }

  void moveFile(const std::string& src, const std::string& dst) {
    Task task;

    task.operation = Task::FILE_MOVE;
    task.src = src;
    task.dst = dst;

    ++taskCnt_;
    addTask(task);
  }

  void deleteFile(const std::string& fileName) {
    Task task;

    task.operation = Task::FILE_DELETE;
    task.src = fileName;
    task.dst = "";

    ++taskCnt_;
    addTask(task);
  }

  void startTask() {
    Task task;
    task.operation = Task::START;
    addTask(task);
  }

  bool renameFile(const std::string& path, const std::string& src, const std::string& dst) {
    auto fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if(fd == -1) return false;

    int result = 0;
    result = syscall(SYS_renameat2, fd, src.c_str(), fd, dst.c_str(), RENAME_NOREPLACE);
    close(fd);

    startTask();
    if(result != -1)
      addLogMessage("rename: " + path + src + " -> " + path + dst);
    else
      addLogMessage("Can't rename file/dir: " + path + src + " -> " + path + dst);

    return result != -1;
  }

  bool chmodFile(const std::string& fileName, mode_t mode) {
    int result = chmod(fileName.c_str(), mode);

    char strMode[80];
    strmode(mode, strMode);
    startTask();

    if(result == 0)
      addLogMessage("chmod[ " + std::string(strMode) + "]: " + fileName);
    else
      addLogMessage("Can't chmod[ " + std::string(strMode) + "]: " + fileName);

    return result == 0;
  }

  bool createFile(const std::string& path, const std::string& name, bool file) {
    auto fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if(fd == -1) return false;

    int result = 0;
    if(file) {
      result = openat(fd, name.c_str(), O_CREAT, 0666);
      close(result);
    }
    else {
      result = mkdirat(fd, name.c_str(), 0766);
    }
    close(fd);

    startTask();
    if(result != -1)
      addLogMessage("Create to file/dir: " + path + name);
    else
      addLogMessage("Can't create to file/dir: " + path + name);

    return result != -1;
  }

  int getTaskCount() const {
    return taskCnt_;
  }

  struct Task {
    enum Operation {
      NONE,
      FILE_COPY,
      FILE_MOVE,
      FILE_DELETE,
      RELOAD,
      START,
      ADD_LOGTEXT,
    } operation;

    std::string src;
    std::string dst;
  };

  std::deque<std::string> getLogText() {
    std::lock_guard<std::mutex> lock(logMutex_);
    logTextUpDate_ = false;
    return logText_;
  }

  bool isLogTextUpdate() const {
    return logTextUpDate_;
  }

  bool hasReloadPath() {
    std::lock_guard<std::mutex> lock(reloadMutex_);

    if(reloadPathQueue_.empty()) return false;
    return true;
  }

  std::string getReloadPath() {
    std::lock_guard<std::mutex> lock(reloadMutex_);
    if(reloadPathQueue_.empty()) return "";

    auto path = reloadPathQueue_.front();
    reloadPathQueue_.pop();

    return path;
  }

private:
  void exec(const std::string& cmd, const std::vector<std::string>& args) {
    int pipefd = 1;
    pid_ = popen2(cmd, args, 0, &pipefd, true);
    if(pid_ < 0) {
      perror("can not exec command");
      return;
    }

    FILE* fp;
    if((fp = fdopen(pipefd, "r")) == NULL) {
      perror("fdopen");

      close(pipefd);
      pclose2(pid_);
      pid_ = 0;

      return;
    }

    char rbuf[4096];
    while(!feof(fp)) {
      if(fgets(rbuf, sizeof(rbuf), fp) != 0)
        addLogText(rbuf);
    }

    pclose2(pid_);
    fclose(fp);
    close(pipefd);
    pid_ = 0;
  }

  void impl() {
    while(!taskEmpty()) {
      auto task = getTask();
      operation_ = true;

      switch(task.operation) {
      case Task::NONE:
        break;

      case Task::FILE_COPY:
      case Task::FILE_MOVE:
        if(task.operation == Task::FILE_COPY){
          std::vector<std::string> args{"-bfvrp", task.src, task.dst};
          exec("cp", args);
        }
        else {
          std::vector<std::string> args{"-bfv", task.src, task.dst};
          exec("mv", args);
        }
        --taskCnt_;
        break;

      case Task::FILE_DELETE:
        {
          std::vector<std::string> args{"-vrf", task.src, task.dst};

          if(config.useTrash())
            exec("trash-put", args);
          else
            exec("rm", args);
        }
        --taskCnt_;
        break;

      case Task::RELOAD:
        addReloadPath(task.src);
        break;

      case Task::START:
        addLogText("");
        break;

      case Task::ADD_LOGTEXT:
        addLogText(task.src);
        break;

      };
      operation_ = false;
      if(kill_) break;
    }
  }

  bool taskEmpty() {
    std::lock_guard<std::mutex> lock(taskMutex_);
    return taskQueue_.empty();
  }

  int getTaskQueueCount() {
    std::lock_guard<std::mutex> lock(taskMutex_);
    return taskQueue_.size();
  }

  void addTask(const Task& task) {
    std::lock_guard<std::mutex> lock(taskMutex_);
    if(taskQueue_.size() != 0)
      taskQueue_.push(task);
    else {
      if(!operation_) {
        if(thread_.joinable()) thread_.join();
        taskQueue_.push(task);
        thread_ = std::thread(&FileOperation::impl, this);
      }
      else {
        taskQueue_.push(task);
      }
    }
  }

  Task getTask() {
    std::lock_guard<std::mutex> lock(taskMutex_);

    auto task = taskQueue_.front();
    taskQueue_.pop();

    return task;
  }

  void addLogText(const std::string& txt) {
    std::lock_guard<std::mutex> lock(logMutex_);
    logTextUpDate_ = true;

    if(logText_.size() >= static_cast<size_t>(config.getLogMaxLines()))
      logText_.pop_back();

    logText_.push_front(txt);
  }

  void addReloadPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(reloadMutex_);

    reloadPathQueue_.push(path);
  }

  std::mutex taskMutex_, logMutex_, reloadMutex_;
  std::atomic<bool> operation_, logTextUpDate_, kill_;
  std::thread thread_;
  std::queue<Task> taskQueue_;
  std::queue<std::string> reloadPathQueue_;
  std::atomic<int> pid_, taskCnt_;
  std::deque<std::string> logText_;
};

class Minase {
public:
  enum PickerMode {
    PICKER_NONE,
    PICKER_FILE,
    PICKER_FILES,
    PICKER_DIR,
  };

  Minase(const std::string& initPath, Minase::PickerMode pickerMode, const std::string& filename) :
    fileViews_({std::unique_ptr<FileView>(new FileView(initPath)),
                std::unique_ptr<FileView>(new FileView(initPath)),
                std::unique_ptr<FileView>(new FileView(initPath)),
                std::unique_ptr<FileView>(new FileView(initPath))}),
    preView_(fileViews_[0] -> getCurrentFileInfo()),
    currentFileView_(0), pickerMode_(pickerMode) {
    tmpFileName_ = TMP_FILENAME;
    pickerOutput_ = tilde2home(filename);
  }

  ~Minase() {
    remove(tmpFileName_.c_str());
  }

  int menuMode(const std::string& title,
               const std::vector<std::string>& menuItems,
               char e = 0) {
    struct tb_event ev;
    int cursor = 0, scrollTop = 0;
    preView_.clear();
    fflush(stdout);
    tb_clear();
    drawMenuMode(title, menuItems, scrollTop, cursor);

    while(1) {
      auto eventStatus = tb_peek_event(&ev, 20);
      if(!eventStatus) continue;

      tb_clear();

      switch (ev.type) {
      case TB_EVENT_KEY:
        switch (ev.key) {
        case TB_KEY_ARROW_LEFT:
          ev.ch = 'h';
          break;

        case TB_KEY_ARROW_RIGHT:
          ev.ch = 'l';
          break;

        case TB_KEY_ARROW_DOWN:
          ev.ch = 'j';
          break;

        case TB_KEY_ARROW_UP:
          ev.ch = 'k';
          break;

        case TB_KEY_ENTER:
          ev.ch = 'l';
          break;

        case TB_KEY_PGDN:
        case TB_KEY_CTRL_D:
          if(menuItems.size() != 0) {
            int scroll = (tb_height() - 5) / 2;
            if(cursor + scroll < static_cast<int>(menuItems.size())) {
              cursor += scroll;
            }
            else {
              cursor = menuItems.size() - 1;
            }

            if(cursor > scroll) {
              if(cursor + scroll < static_cast<int>(menuItems.size())) {
                scrollTop = cursor - scroll;
              }
              else {
                scrollTop = menuItems.size() - (tb_height() - 4);
                if(scrollTop < 0) scrollTop = 0;
              }
            }
            else scrollTop = 0;

          }
          break;

        case TB_KEY_PGUP:
        case TB_KEY_CTRL_U:
          if(menuItems.size() != 0) {
            int scroll = (tb_height() - 5) / 2;
            if(cursor - scroll > 0) {
              cursor -= scroll;

              if(cursor - scroll > 0)
                scrollTop = cursor - scroll;
              else scrollTop = 0;
            }
            else {
              cursor = 0;
              scrollTop = 0;
            }
            break;
          }
          break;

        case TB_KEY_HOME:
          ev.ch = 'g';
          break;

        case TB_KEY_END:
          ev.ch = 'G';
          break;

        case TB_KEY_CTRL_C:
          return -1;

        };

        switch (ev.ch) {
        case 'q':
        case 'h':
          return -1;

        case 'l':
          return cursor;

        case 'H':
          cursor = scrollTop;
          break;

        case 'L':
          if(menuItems.size() != 0) {
            cursor = scrollTop + tb_height() - 5;
            if(scrollTop + tb_height() - 5 > static_cast<int>(menuItems.size() - 1))
              cursor = menuItems.size() - 1;
          }
          break;

        case 'M':
          if(menuItems.size() != 0) {
            cursor = scrollTop + (tb_height() - 5) / 2;
            if(scrollTop + tb_height() - 5 > static_cast<int>(menuItems.size() - 1))
              cursor = (menuItems.size() - 1) / 2;
          }
          break;

        case 'g':
          if(menuItems.size() != 0) {
            cursor = scrollTop = 0;
          }
          break;

        case 'G':
          if(menuItems.size() != 0) {
            cursor = menuItems.size() - 1;
            scrollTop = cursor - (tb_height() - 5);
            if(scrollTop < 0) scrollTop = 0;
          }
          break;

        case 'j':
          if(menuItems.size() != 0) {
            if(cursor < static_cast<int>(menuItems.size() - 1))
              ++cursor;

            if(cursor > scrollTop + tb_height() - 5) ++scrollTop;
          }
          break;

        case 'k':
          if(menuItems.size() != 0) {
            if(cursor > 0) {
              --cursor;
            }
            if(cursor < scrollTop) --scrollTop;
          }

          break;

        default:
          if(e != 0 && ev.ch == static_cast<uint32_t>(e)) return -1;
        }
        break;

      case TB_EVENT_RESIZE:
        break;
      }

      drawMenuMode(title, menuItems, scrollTop, cursor);
    }
  }

  void drawMenuMode(const std::string& title, const std::vector<std::string>& menuItems, int scrollTop, int cursor) {

    drawText(tb_width() / 2 + 2, 1, "[");
    int len = drawText(tb_width() / 2 + 3, 1, title, TB_CYAN | TB_BOLD);
    drawText(tb_width() / 2 + 2 + len + 1, 1, "]");

    std::string txt = std::to_string(cursor);
    txt += "/" + std::to_string((menuItems.size() == 0) ? 0 : menuItems.size() - 1);
    drawText(tb_width() - 2 - txt.length(), 1, txt.c_str());

    for(int i = 0; i < tb_height() - 4; ++i) {
      int color = 0;
      if(i + scrollTop == cursor) color = TB_REVERSE;
      if(i + scrollTop < static_cast<int>(menuItems.size())) {
        auto txt = menuItems[i + scrollTop];
        if(txt.back() == '\n') txt.pop_back();

        drawText(tb_width() / 2 + 2, i + 2, txt, color);
      }
    }

    printTask();
    printCurrentPath(fileViews_[currentFileView_] -> getPath());

    printCurrentFileInfo();
    printInfoMessage("");

    fileViews_[currentFileView_] -> draw();

    tb_present();
  }

  void logViewMode() {
    int line = 0, oldTaskCnt = 0;

    struct tb_event ev;
    auto logText = fileOperation_.getLogText();

    preView_.clear();
    fflush(stdout);
    tb_clear();
    drawLogViewMode(logText, line);

    while(1) {
      auto eventStatus = tb_peek_event(&ev, 20);
      if(fileOperation_.isLogTextUpdate()) {
        logText = fileOperation_.getLogText();
        tb_clear();
        drawLogViewMode(logText, line);
      }

      if(oldTaskCnt != fileOperation_.getTaskCount()) {
        oldTaskCnt = fileOperation_.getTaskCount();
        printTask();
        tb_present();
        fflush(stdout);
      }

      if(!eventStatus) continue;
      tb_clear();

      switch (ev.type) {
      case TB_EVENT_KEY:
        switch (ev.key) {
        case TB_KEY_ARROW_DOWN:
          ev.ch = 'j';
          break;

        case TB_KEY_ARROW_UP:
          ev.ch = 'k';
          break;

        case TB_KEY_PGDN:
        case TB_KEY_CTRL_D:
          if(static_cast<int>(logText.size()) >
             line + tb_height() + tb_height() / 2) {
            line += tb_height() / 2;
          }
          else line = logText.size() - tb_height();
          break;

        case TB_KEY_PGUP:
        case TB_KEY_CTRL_U:
          if(line - tb_height() / 2 > 0) {
            line -= tb_height() / 2;
          }
          else line = 0;
          break;

        case TB_KEY_HOME:
          ev.ch = 'g';
          break;

        case TB_KEY_END:
          ev.ch = 'G';
          break;
        }

        switch (ev.ch) {
        case 'q':
        case '0':
          return;

        case 'j':
          if(static_cast<int>(logText.size()) > line + tb_height()) {
            ++line;
          }
          break;

        case 'k':
          if(line > 0) {
            --line;
          }
          break;
        }
        break;

      case TB_EVENT_RESIZE:
        break;
      }
      drawLogViewMode(logText, line);
    }
  }

  void drawLogViewMode(const std::deque<std::string>& logText, int line) {
    drawText(0, 0, "[LogViewer]", TB_CYAN | TB_BOLD);

    std::string txt;
    for(int j = 0; j < tb_width(); ++j)
      txt += '-';
    drawText(0, 1, txt);

    for(int i = 0; i < tb_height() - 2; ++i) {
      if(i + line < static_cast<int>(logText.size())) {
        auto txt = logText[i + line];
        if(txt.back() == '\n') txt.pop_back();

        if(txt == "") {
          for(int j = 0; j < tb_width(); ++j)
            txt += '-';
        }
        drawText(0, i + 2, txt);
      }
    }
    printTask();
    tb_present();
  }

  void setSizeFileViews() {
    for(auto&& fileView: fileViews_) {
      fileView -> setSize(tb_width() / 2 - 1, tb_height() - 3);
      fileView -> setPosition(0, 1);
    }
  }

  void run() {
    struct tb_event ev;
    bool preViewDraw = false;
    int oldTaskCnt = 0;

    tb_clear();
    setSizeFileViews();
    preView_.setPosition(tb_width() / 2 + 2, 1);
    preView_.setSize(tb_width() / 2 - 4, tb_height() - 3);
    draw();

    while(1) {
      auto eventStatus = tb_peek_event(&ev, 20);

      if(eventStatus == 0) {
        if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
          if(preView_.getLoadFileName() != fileViews_[currentFileView_] -> getCurrentFileName()) {
            preView_.setLoadFile(fileViews_[currentFileView_] -> getCurrentFileInfo());
            preView_.setDisable(false);
            preViewDraw = false;
          }
        }
        else {
          if(!preView_.isDisable()) {
            preView_.setLoadFile(FileInfo("", ""));
            preView_.setDisable(true);
            preViewDraw = false;
          }
        }
      }

      if(oldTaskCnt != fileOperation_.getTaskCount()) {
        oldTaskCnt = fileOperation_.getTaskCount();
        printTask();
        tb_present();
      }

      if(fileOperation_.hasReloadPath()) {
        auto path = fileOperation_.getReloadPath();
        if(fileViews_[currentFileView_] -> getPath() == path) {
          fileViews_[currentFileView_] -> reload();

          tb_clear();
          draw();
          tb_present();
        }

        for(auto&& fileView: fileViews_) {
          if(fileViews_[currentFileView_] == fileView) continue;

          if(fileView -> getPath() == path) {
            fileView -> reload();
          }
        }
      }

      if(!preViewDraw) {
        preViewDraw = preView_.draw();
      }

      if(!eventStatus) continue;
      tb_clear();

      switch (ev.type) {
      case TB_EVENT_KEY:
        if(!eventKey(ev.key, ev.ch, ev.mod, preViewDraw)) return;
        break;

      case TB_EVENT_RESIZE:
        resize();
        preViewDraw = false;
        break;
      }

      draw();
    }
  }

  void draw() {
    printTask();
    printCurrentPath(fileViews_[currentFileView_] -> getPath());

    printCurrentFileInfo();
    printInfoMessage("");

    fileViews_[currentFileView_] -> draw();
    tb_present();
  }

private:
  void resize() {
    if((fileViews_[currentFileView_] -> getWidth() == tb_width() / 2 - 1) &&
       (fileViews_[currentFileView_] -> getHeight() == tb_height() - 3)) return;

    setSizeFileViews();
    preView_.setPosition(tb_width() / 2 + 2, 1);
    preView_.setSize(tb_width() / 2 - 4, tb_height() - 3);
  }

  bool eventKey(uint16_t key, uint32_t ch, uint8_t mod, bool& preViewDraw) {
    if(mod & TB_MOD_ALT) {
      for(const auto& plugin: config.getPlugins()) {
        if(!plugin.key.empty()) {
          if(ch == static_cast<uint32_t>(plugin.key[0])) {
            execPlugin(plugin);
            preViewDraw = false;
            return true;
          }
        }
      }
      return true;
    }

    switch (key) {
    case TB_KEY_ARROW_DOWN:
      ch = 'j';
      break;

    case TB_KEY_ARROW_UP:
      ch = 'k';
      break;

    case TB_KEY_ARROW_LEFT:
      ch = 'h';
      break;

    case TB_KEY_ARROW_RIGHT:
      ch = 'l';
      break;

    case TB_KEY_SPACE:
      selectFile();
      break;

    case TB_KEY_ENTER:
      if(pickerMode_ != PICKER_NONE) {
        if(picker()) return(false);
      }
      break;

    case TB_KEY_PGDN:
    case TB_KEY_CTRL_D:
      fileViews_[currentFileView_] -> cursorPgDn();
      break;

    case TB_KEY_PGUP:
    case TB_KEY_CTRL_U:
      fileViews_[currentFileView_] -> cursorPgUp();
      break;

    case TB_KEY_HOME:
      ch = 'g';
      break;

    case TB_KEY_END:
      ch = 'G';
      break;

    case TB_KEY_CTRL_A:
      gotoDirectory(config.getArchiveMntDir());
      break;

    case TB_KEY_CTRL_J:
      if(preView_.scrollDown()) preViewDraw = false;
      break;

    case TB_KEY_CTRL_K:
      if(preView_.scrollUp()) preViewDraw = false;
      break;

    case TB_KEY_CTRL_R:
      spawn("vidir", "", "", "", fileViews_[currentFileView_] -> getPath());
      fileViews_[currentFileView_] -> reload();
      resize();
      preViewDraw = false;
      break;

    case TB_KEY_CTRL_L:
      refresh();
      fileViews_[currentFileView_] -> reload();
      preViewDraw = false;
      break;

    case TB_KEY_CTRL_O:
      if(openWith()) preViewDraw = false;
      break;

    case TB_KEY_CTRL_SLASH:
      setFileViewFilterType();
      break;

    case TB_KEY_CTRL_X:
      pluginMenu();
      preViewDraw = false;
      break;

    case TB_KEY_CTRL_G:
      {
        auto c = getInput("Quit? (y/N)");
        if(c == 'y' || c == 'Y') {
          auto home = getenv("HOME");

          if(home != 0) {
            std::string filename = std::string(home) + "/.config/Minase/lastdir";
            FILE *fp;

            if((fp = fopen(filename.c_str(), "w")) != NULL) {
              fprintf(fp, "%s",
                      fileViews_[currentFileView_] -> getPath().c_str());
              fclose(fp);
            }
          }
          return false;
        }
        else return true;
      }
    };

    switch (ch) {
    case 'q':
      {
        auto c = getInput("Quit? (y/N)");
        if(c == 'y' || c == 'Y') return false;
        else return true;
      }

    case '0':
      logViewMode();
      resize();
      preViewDraw = false;
      break;

    case '1':
    case '2':
    case '3':
    case '4':
      currentFileView_ = ch - '1';
      refresh();
      fileViews_[currentFileView_] -> reload();
      preViewDraw = false;
      break;

    case '!':
      {
        auto shell = getenv("SHELL");

        if(shell == 0) printInfoMessage("SHELL environment variable not set.");
        else {
          spawn(shell, "", "", "",
                fileViews_[currentFileView_] -> getPath());
          resize();
          fileViews_[currentFileView_] -> reload();
          preViewDraw = false;
        }
      }
      break;

    case '@':
      {
        auto home = getenv("HOME");

        if(home == 0) fileViews_[currentFileView_] -> setPath("/");
        else fileViews_[currentFileView_] -> setPath(std::string(home) + "/");
      }
      break;

    case 'a':
      invertSelection();
      break;

    case 'b':
      openBookmarks();
      resize();
      preViewDraw = false;
      break;

    case 'H':
      fileViews_[currentFileView_] -> cursorMoveTopOfScreen();
      break;

    case 'M':
      fileViews_[currentFileView_] -> cursorMoveMiddleOfScreen();
      break;

    case 'L':
      fileViews_[currentFileView_] -> cursorMoveBottomOfScreen();
      break;

    case 'e':
      if(editFile()) preViewDraw = false;
      break;

    case '/':
      setFileViewFilter();
      break;

    case 'i':
      toggleImagePreview();
      preViewDraw = false;
      break;

    case 'g':
      fileViews_[currentFileView_] -> setCursorPos(0);
      break;

    case 'G':
      fileViews_[currentFileView_] ->
        setCursorPos(fileViews_[currentFileView_] -> getFileListCount() - 1);
      break;

    case 'Z':
      fileViews_[currentFileView_] ->
        setCursorPos(fileViews_[currentFileView_] -> getCursorPos());
      break;

    case ',':
      changeFileViewType();
      break;

    case 's':
      sortFiles();
      break;

    case '*':
      toggleExecutePermission();
      break;

    case 'r':
      renameFile();
      break;

    case 'n':
      createFile();
      break;

    case 'c':
      copyFiles();
      break;

    case 'm':
      moveFiles();
      break;

    case 'd':
      deleteFiles();
      break;

    case 'p':
      pasteFiles();
      break;

    case '.':
      toggleShowHiddenFiles();
      break;

    case 'u':
      fileViews_[currentFileView_] -> clearSelectFiles();
      break;

    case 'U':
      unmount();
      break;

    case 'j':
      fileViews_[currentFileView_] -> cursorNext();
      break;

    case 'k':
      fileViews_[currentFileView_] -> cursorPrev();
      break;

    case 'l':
      openFile();
      break;

    case 'h':
      if(!fileViews_[currentFileView_] -> upDir()) printInfoMessage(strerror(errno));
      break;

    case 'x':
      extractArchive();
      break;

    case 'z':
      createArchive();
      break;
    };

    return true;
  }

  void createArchive() {
    auto selectFiles = fileViews_[currentFileView_] -> getSelectFiles();
    std::string defaultArchiveName;
    std::string name;

    auto shell = getenv("SHELL");
    if(shell == 0) {
      printInfoMessage("SHELL environment variable not set.");
      return;
    }

    if(selectFiles.size() == 0) {
      defaultArchiveName = fileViews_[currentFileView_] -> getCurrentFileName();

      if(fileViews_[currentFileView_] -> getCurrentFileInfo().isDir())
        defaultArchiveName.pop_back();
    }

    if(!getReadline("Archive: ", name, defaultArchiveName.c_str())) {
      if(selectFiles.size() != 0) {
        FILE* fp;
        if((fp = fopen(tmpFileName_.c_str(), "w")) == NULL) return;
        for(const auto &f : selectFiles) {
          auto current = fileViews_[currentFileView_] -> getPath();
          current.pop_back();

          std::string filename;
          auto i = f.find(current);
          if(i == 0) filename = "./" + f.substr(current.length() + 1);
          else filename = f;

          fprintf(fp, "%s%c", (filename).c_str(), '\0');
        }
        fclose(fp);

        std::string cmd;
        if(getSuffix(name) == "7z") {
          cmd = "cat \"" + tmpFileName_ + "\" | xargs -0 apack \"" + name + "\"";
        }
        else {
          cmd = "apack --null \"" + name + "\" < \"" + tmpFileName_ + "\"";
        }

        spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath());
      }
      else {
        std::string cmd = "apack \"" + name + "\" " + "\"" + fileViews_[currentFileView_] -> getCurrentFileName() + "\"";
        spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath());
      }
      fileViews_[currentFileView_] -> update();

      int pos = fileViews_[currentFileView_] -> searchFileName(name);
      if(pos != -1) fileViews_[currentFileView_] -> setCursorPos(pos);
      else fileViews_[currentFileView_] -> setCursorPos(0);
    }
  }

  bool picker() {
    FILE *fp;

    if(pickerMode_ == PICKER_FILES) {
      auto selectFiles = fileViews_[currentFileView_] -> getSelectFiles();
      if(!selectFiles.empty()) {
        if((fp = fopen(pickerOutput_.c_str(), "w")) != NULL) {
          for(const auto& f: selectFiles) {
            fprintf(fp, "%s\n", f.c_str());
          }
          fclose(fp);
        }
        return true;
      }
    }

    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      auto file = fileViews_[currentFileView_] -> getCurrentFileInfo();
      if((pickerMode_ == PICKER_FILE && !file.isDir()) ||
         (pickerMode_ == PICKER_DIR && file.isDir()) ||
         (pickerMode_ == PICKER_FILES)) {

        if((fp = fopen(pickerOutput_.c_str(), "w")) != NULL) {
          fprintf(fp, "%s\n", file.getFilePath().c_str());
          fclose(fp);
        }

        return true;
      }
    }

    return false;
  }

  void execPlugin(const Config::Plugin& plugin) {
    std::string filepath = tilde2home(plugin.filePath);

    FILE* fp;
    if((fp = fopen(tmpFileName_.c_str(), "w")) == NULL) return;

    auto selectFiles = fileViews_[currentFileView_] -> getSelectFiles();
    for(const auto& f: selectFiles) {
      fprintf(fp, "%s\n", f.c_str());
    }
    fclose(fp);

    std::string text;
    if(plugin.inputText) {
      if(!plugin.filePath.empty()) {
        std::vector<std::string> currentDirFiles;
        for(int i = 0; i < fileViews_[currentFileView_] -> getFileListCount(); ++i)
          currentDirFiles.emplace_back(fileViews_[currentFileView_] -> getFileInfo(i).getFileName());

        if(getReadline(plugin.name + ": ", text, 0, &currentDirFiles)) return;
      }
    }

    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      spawn(filepath, fileViews_[currentFileView_] -> getCurrentFileName(),
            tmpFileName_, text, fileViews_[currentFileView_] -> getPath(),
            plugin.gui, plugin.silent);
    }
    else {
      spawn(filepath, fileViews_[currentFileView_] -> getPath(),
            tmpFileName_, text, fileViews_[currentFileView_] -> getPath(),
            plugin.gui, plugin.silent);
    }

    if(plugin.operation == Config::Plugin::Operation::CHANGE_DIRECTORY) {
      FILE* fp;
      char buf[4096];

      if((fp = fopen(tmpFileName_.c_str(), "r")) == NULL) return;

      if(fgets(buf, sizeof(buf), fp) != 0) {
        if(buf[0] != '\0') gotoDirectory(buf);
      }

      fclose(fp);
      resize();
    }
    else if(plugin.operation == Config::Plugin::Operation::CHANGE_CURRENT_FILE) {
      FILE* fp;
      char buf[4096];

      if((fp = fopen(tmpFileName_.c_str(), "r")) == NULL) return;

      if(fgets(buf, sizeof(buf), fp) != 0) {
        if(buf[0] != '\0') {
          std::string fileName;

          if(getDirName(buf) == "") {
            fileViews_[currentFileView_] -> update();
            fileName = buf;
          }
          else {
            if(!gotoDirectory(getDirName(buf))) {
              resize();
              fclose(fp);
              return;
            }
            else fileName = getBaseName(buf);
          }

          bool hidden = false;
          if(!fileName.empty() && fileName[0] == '.') {
            if(!fileViews_[currentFileView_] -> isShowHiddenFiles()) {
              hidden = true;
              fileViews_[currentFileView_] -> showHiddenFiles(true);
            }
          }

          int pos = fileViews_[currentFileView_] -> searchFileName(fileName);
          if(pos != -1) fileViews_[currentFileView_] -> setCursorPos(pos);
          else {
            pos = fileViews_[currentFileView_] -> searchFileName(fileName + "/");

            if(pos != -1) fileViews_[currentFileView_] -> setCursorPos(pos);
            else {
              fileViews_[currentFileView_] -> setCursorPos(0);
              if(hidden) fileViews_[currentFileView_] -> showHiddenFiles(false);
            }
          }
        }
      }
      else fileViews_[currentFileView_] -> reload();

      resize();
      fclose(fp);
    }
    else {
      resize();
      fileViews_[currentFileView_] -> reload();
    }
  }

  void pluginMenu() {
    auto plugins = config.getPlugins();
    std::vector<std::string> pluginNames;
    for(const auto& plugin: plugins) {
      char k = ' ';

      if(!plugin.key.empty()) k = plugin.key[0];
      pluginNames.emplace_back(std::string("[") + k + "]" + " : " + plugin.name);
    }

    int n = menuMode("Plugins", pluginNames, 'x');
    if(n != -1 && plugins.size() > 0)
      execPlugin(plugins[n]);
  }

  std::string tilde2home(const std::string& path) {
    if(path.length() >= 2 && path[0] == '~' && path[1] == '/') {
      auto home = getenv("HOME");
      if(home == 0)
        return std::string(path.begin() + 1, path.end());
      else
        return home + std::string(path.begin() + 1, path.end());
    }
    else return path;
  }

  bool gotoDirectory(const std::string path) {
    if(path.empty()) return false;

    auto orgPath = tilde2home(path);

    auto p = orgPath.c_str();
    if((p[0] == '.' && p[1] == '.') || (p[0] != '/') ||
       ((p[0] == '.' && p[1] == '\0') || (p[0] == '.' && p[1] == '/'))) {
      orgPath = fileViews_[currentFileView_] -> getPath() + orgPath;
    }

    auto rpath = realpath(orgPath.c_str(), NULL);
    if(rpath != NULL) {
      if(std::string(rpath) != "/")
        fileViews_[currentFileView_] -> setPath(std::string(rpath) + "/");
      else
        fileViews_[currentFileView_] -> setPath("/");

      free(rpath);
      return true;
    }
    else {
      printInfoMessage(strerror(errno));
      return false;
    }
  }

  void openBookmarks() {
    auto bookmarks = config.getBookmarks();
    int n = menuMode("BookMark", bookmarks, 'b');
    if(n != -1 && bookmarks.size() > 0) gotoDirectory(bookmarks[n]);
  }

  void invertSelection() {
    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      for(int i = 0; i < fileViews_[currentFileView_] -> getFileListCount(); ++i) {
        auto fileInfo = fileViews_[currentFileView_] -> getFileInfo(i);

        if(fileViews_[currentFileView_] -> isSelectedFile(fileInfo))
          fileViews_[currentFileView_] -> unselectFile(fileInfo);
        else
          fileViews_[currentFileView_] -> selectFile(fileInfo);
      }
    }
  }

  void openFile() {
    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      auto fileInfo = fileViews_[currentFileView_] -> getCurrentFileInfo();
      auto newPath = fileInfo.getFilePath();

      if(fileInfo.isDir()) {
        if(!fileViews_[currentFileView_] -> setPath(newPath)) printInfoMessage(strerror(errno));
      }
      else {
        FILE* fp;
        if ((fp = fopen(fileInfo.getFilePath().c_str(), "rb")) != NULL) {
          auto archive = CheckFileType::isArchive(fp);
          fclose(fp);

          if(archive) {
            openArchive();
            fileViews_[currentFileView_] -> reload();
            resize();
          }
          else {
            spawn(config.getOpener(),
                  fileViews_[currentFileView_]->getCurrentFilePath().c_str(),
                  "", "", "", true);
          }
        }
      }
    }
  }

  void openArchive() {
    auto c = getInput("'o'pen / e'x'tract / 'l's / 'm'nt");
    auto shell = getenv("SHELL");
    if(shell == 0) {
      printInfoMessage("SHELL environment variable not set.");
      return;
    }

    if(c == 'o') {
      spawn(config.getOpener(),
                  fileViews_[currentFileView_]->getCurrentFilePath().c_str(),
                  "", "", "", true);
    }
    else if(c == 'l') {
      auto lsar = which("lsar");
      auto bsdtar = which("bsdtar");
      if(!lsar && !bsdtar) {
        printInfoMessage("install 'lsar' or 'bsdtar'");
        return;
      }

      std::string cmd;
      if(lsar)
        cmd = "lsar -l \"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\" | less";
      else if(bsdtar)
        cmd = "bsdtar tfv \"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\" | less";

      spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath());
    }
    else if(c == 'x') {
      extractArchive(true);
    }
    else if(c == 'm') {
      if(!which("archivemount")) {
        printInfoMessage("install 'archivemount'");
        return;
      }

      std::string archiveMntDir = tilde2home(config.getArchiveMntDir());
      std::string mntdir = archiveMntDir + "/" + fileViews_[currentFileView_]->getCurrentFileName();
      struct stat stat_buf;
      if(lstat(archiveMntDir.c_str(), &stat_buf) != 0) {
        std::string cmd = "mkdir -p \"" + archiveMntDir + "\" > /dev/null 2>&1";
        spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath(), false, true);
      }

      if(mkdir(mntdir.c_str(), 0777) == -1) {
        printInfoMessage(mntdir + " : " + strerror(errno));
        return;
      }

      std::string cmd = "archivemount \"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\" \"" + mntdir + "\" > /dev/null 2>&1";
      if(spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath(), false, true) != 0) {
        printInfoMessage("archivemount failed: " + mntdir);
        rmdir(mntdir.c_str());
      }
      else printInfoMessage("archivemount: " + mntdir);
    }
  }

  void unmount() {
    auto shell = getenv("SHELL");
    if(shell == 0) {
      printInfoMessage("SHELL environment variable not set.");
      return;
    }

    std::string cmd;
    if(which("fusermount3"))
      cmd = "fusermount3 -u \"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\" > /dev/null 2>&1";
    else if(which("fusermount"))
      cmd = "fusermount -u \"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\" > /dev/null 2>&1";
    else {
      printInfoMessage("install 'fusermount3' or 'fusermount'");
      return;
    }

    if(spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath(), false, true) != 0) {
      printInfoMessage("fusermount3 failed!");
    }
    else {
      rmdir(fileViews_[currentFileView_]->getCurrentFilePath().c_str());
    }
    fileViews_[currentFileView_] -> reload();
    resize();
  }

  void extractArchive(bool currentOnly = false) {
    auto shell = getenv("SHELL");
    if(shell == 0) {
      printInfoMessage("SHELL environment variable not set.");
      return;
    }

    auto unar = which("unar");
    auto bsdtar = which("bsdtar");
    if(!unar && !bsdtar) {
      printInfoMessage("install 'unar' or 'bsdtar'");
      return;
    }

    auto selectFiles = fileViews_[currentFileView_] -> getSelectFiles();
    std::string cmd;
    if(currentOnly || selectFiles.size() == 0) {
      std::string dir = "./";

      if(unar)
        cmd = "unar -o \"" + dir + "\" " + "\"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\"";
      else if(bsdtar)
        cmd = "bsdtar -C \"" + dir + "\" -xvf " + "\"" + fileViews_[currentFileView_]->getCurrentFilePath() + "\"";
    }
    else {
      FILE* fp;
      if((fp = fopen(tmpFileName_.c_str(), "w")) == NULL) return;
      for(const auto &f : selectFiles) {
        fprintf(fp, "%s%c", f.c_str(), '\0');
      }
      fclose(fp);

      if(unar) {
        cmd = "cat \"" + tmpFileName_ + "\" | xargs -0 -n 1 unar ";
      }
      else if(bsdtar) {
        cmd = "cat \"" + tmpFileName_ + "\" | xargs -0 -n 1 bsdtar -xvf ";
      }
    }

    tb_shutdown();
    spawn(shell, "-c", cmd, "", fileViews_[currentFileView_]->getPath(), false, true);
    printf("\e[7mPress Enter key!!\n\e[0m");
    getchar();
    tb_init();

    fileViews_[currentFileView_] -> reload();
    resize();
  }

  bool toggleExecutePermission() {
    if(fileViews_[currentFileView_] -> isFileListEmpty()) return false;

    auto fileInfo = fileViews_[currentFileView_] -> getCurrentFileInfo();
    auto mode = fileInfo.getMode();

    if(mode & S_IXUSR)
      mode &= ~(S_IXUSR | S_IXGRP | S_IXOTH);
    else
      mode |= (S_IXUSR | S_IXGRP | S_IXOTH);

    if(fileOperation_.chmodFile(fileViews_[currentFileView_] -> getCurrentFilePath(), mode))
      fileViews_[currentFileView_] -> update();
    else printInfoMessage(strerror(errno));

    return true;
  }

  bool renameFile() {
    if(fileViews_[currentFileView_] -> isFileListEmpty()) return false;
    std::string name;
    if(!getReadline("Rename: ", name, fileViews_[currentFileView_] -> getCurrentFileName().c_str())) {
      if(fileOperation_.renameFile(fileViews_[currentFileView_] -> getPath(),
                                   fileViews_[currentFileView_] -> getCurrentFileName(),
                                   name)) {
        fileViews_[currentFileView_] -> update();

        int pos = fileViews_[currentFileView_] -> searchFileName(name);
        if(pos != -1) fileViews_[currentFileView_] -> setCursorPos(pos);
        else fileViews_[currentFileView_] -> setCursorPos(0);
      }
      else printInfoMessage(strerror(errno));
    }

    return true;
  }

  bool createFile() {
    std::string name;
    if(!getReadline("Create: ", name)) {
      auto c = getInput("'f'(ile) / 'd'(ir)");
      bool file;

      if(c == 'f' || c == 'F') file = true;
      else if(c == 'd' || c == 'D') file = false;
      else return false;

      if(fileOperation_.createFile(fileViews_[currentFileView_] -> getPath(), name, file)) {
        fileViews_[currentFileView_] -> update();

        int pos = fileViews_[currentFileView_] -> searchFileName(name);
        if(pos != -1) fileViews_[currentFileView_] -> setCursorPos(pos);
        else fileViews_[currentFileView_] -> setCursorPos(0);
      }
      else printInfoMessage(strerror(errno));
    }

    return true;
  }

  void selectFile() {
    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      if(fileViews_[currentFileView_] -> isSelectedFile(fileViews_[currentFileView_] -> getCurrentFileInfo()))
        fileViews_[currentFileView_] -> unselectFile(fileViews_[currentFileView_] -> getCurrentFileInfo());
      else
        fileViews_[currentFileView_] -> selectFile(fileViews_[currentFileView_] -> getCurrentFileInfo());

      fileViews_[currentFileView_] -> cursorNext();
    }
  }

  bool copyFiles() {
    buffer_.selectedFiles = fileViews_[currentFileView_] -> getSelectFiles();
    if(!buffer_.selectedFiles.empty()) {
      buffer_.operation = FileOperation::Task::FILE_COPY;
      fileViews_[currentFileView_] -> clearSelectFiles();

      return true;
    }

    return false;
  }

  bool moveFiles() {
    buffer_.selectedFiles = fileViews_[currentFileView_] -> getSelectFiles();
    if(!buffer_.selectedFiles.empty()) {
      buffer_.operation = FileOperation::Task::FILE_MOVE;
      fileViews_[currentFileView_] -> clearSelectFiles();

      return true;
    }

    return false;
  }

  bool deleteFiles() {
    buffer_.selectedFiles = fileViews_[currentFileView_] -> getSelectFiles();
    if(!buffer_.selectedFiles.empty()) {
      auto c = getInput("delete?(y/N)");
      if(c == 'y' || c== 'Y') {
        buffer_.operation = FileOperation::Task::FILE_DELETE;
        fileViews_[currentFileView_] -> clearSelectFiles();

        fileOperation_.startTask();
        for(const auto& file: buffer_.selectedFiles) {
          fileOperation_.deleteFile(file);
        }
        fileOperation_.reloadPath(fileViews_[currentFileView_] -> getPath());

        buffer_.selectedFiles.clear();
        buffer_.operation = FileOperation::Task::NONE;

        return true;
      }
    }

    return false;
  }

  bool pasteFiles() {
    if(!buffer_.selectedFiles.empty()) {
      if(buffer_.operation == FileOperation::Task::FILE_COPY ||
         buffer_.operation == FileOperation::Task::FILE_MOVE) {
        fileOperation_.startTask();

        for(const auto& file: buffer_.selectedFiles) {
          if(buffer_.operation == FileOperation::Task::FILE_COPY)
            fileOperation_.copyFile(file, fileViews_[currentFileView_] -> getPath());
          else if(buffer_.operation == FileOperation::Task::FILE_MOVE)
            fileOperation_.moveFile(file, fileViews_[currentFileView_] -> getPath());
        }
        fileOperation_.reloadPath(fileViews_[currentFileView_] -> getPath());

        if(buffer_.operation == FileOperation::Task::FILE_MOVE) {
          buffer_.selectedFiles.clear();
          buffer_.operation = FileOperation::Task::NONE;
        }

        return true;
      }
    }

    return false;
  }

  bool sortFiles() {
    auto c1 = getInput("Sort by 'n'(ame) / 's'(ize) / 't'(ime)");
    if(!(c1 == 'n' || c1 == 's' || c1 == 't')) return false;

    auto c2 = getInput("Order by 'a'(sc) / 'd'(esc)");
    if(!(c2 == 'a' || c2 == 'd')) return false;

    DirInfo::SortType type;
    DirInfo::SortOrder order;

    switch(c1) {
    case 'n':
      type = DirInfo::SortType::NAME;
      break;
    case 's':
      type = DirInfo::SortType::SIZE;
      break;
    case 't':
      type = DirInfo::SortType::DATE;
      break;
    default:
      type = DirInfo::SortType::NAME;
    };

    if(c2 == 'a') order = DirInfo::SortOrder::ASCENDING;
    else order = DirInfo::SortOrder::DESCENDING;

    fileViews_[currentFileView_] -> sort(type, order);

    return true;
  }

  bool openWith() {
    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      std::string cmd;

      if(cmdCache_.empty()) updateCmdCache();
      if(!getReadline("open with: ", cmd, 0, &cmdCache_)) {
        auto c = getInput("cli mode? (y/N)");
        bool gui = true;
        if(c == 'y' || c == 'Y') gui = false;
        spawn(cmd, fileViews_[currentFileView_] -> getCurrentFileName(), "", "",
              fileViews_[currentFileView_] -> getPath(), gui);

        resize();
        fileViews_[currentFileView_] -> reload();
        return true;
      }
    }

    return false;
  }

  bool editFile() {
    if(!fileViews_[currentFileView_] -> isFileListEmpty()) {
      auto editor = getenv("EDITOR");
      if(editor == 0) {
        printInfoMessage("EDITOR environment variable not set.");
        return false;
      }

      spawn(editor,
            fileViews_[currentFileView_] -> getCurrentFilePath().c_str(),
            "", "", fileViews_[currentFileView_] -> getPath());
      resize();

      preView_.setLoadFile(fileViews_[currentFileView_] -> getCurrentFileInfo());
      preView_.setDisable(false);

      return true;
    }

    return false;
  }

  void toggleShowHiddenFiles() {
    fileViews_[currentFileView_] -> showHiddenFiles(!fileViews_[currentFileView_] -> isShowHiddenFiles());

    if(fileViews_[currentFileView_] -> isShowHiddenFiles())
      printInfoMessage("Show dot files.");
    else
      printInfoMessage("Hide dot files.");
  }

  void toggleImagePreview() {
    preView_.setImagePreview(!preView_.isImagePreview());
    preView_.reload();
    refresh();
    fileViews_[currentFileView_] -> reload();

    if(preView_.isImagePreview())
      printInfoMessage("Enable Image Preview.");
    else
      printInfoMessage("Disable Image Preview.");
  }

  void setFileViewFilter() {
    std::string filter;
    char buf[256];
    char typeName[] = {'N', 'R', 'M'};
    snprintf(buf, sizeof(buf), "Filter[%c]: ", typeName[fileViews_[currentFileView_] -> getFilterType()]);

    if(!getReadline(buf, filter, 0, 0, &filterHistory_)) {
      if(fileViews_[currentFileView_] -> getFilter() != filter) {
        fileViews_[currentFileView_] -> filter(filter,
                                               fileViews_[currentFileView_] -> getFilterType());
        fileViews_[currentFileView_] -> setCursorPos(0);

        if(!filter.empty()) {
          if(filterHistory_.end() != std::find(filterHistory_.begin(), filterHistory_.end(), filter)) {
            filterHistory_.remove(filter);
          }
          filterHistory_.emplace_back(filter);
        }
      }
    }
  }

  void setFileViewFilterType() {
#ifdef USE_MIGEMO
    auto c = getInput("'n'(ormal) 'r'(egexp) 'm'(igemo)");
#else
    auto c = getInput("'n'(ormal) 'r'(egexp)");
#endif

    DirInfo::FilterType type = fileViews_[currentFileView_] -> getFilterType();
    switch(c) {
    case 'n':
      type = DirInfo::FilterType::NORMAL;
      break;
    case 'r':
      type = DirInfo::FilterType::REGEXP;
      break;
#ifdef USE_MIGEMO
    case 'm':
      type = DirInfo::FilterType::MIGEMO;
      break;
#endif
    };

    if(fileViews_[currentFileView_] -> getFilterType() == type) return;
    fileViews_[currentFileView_] -> filter(fileViews_[currentFileView_] -> getFilter(), type);
    fileViews_[currentFileView_] -> setCursorPos(0);
  }

  void refresh() {
    for(auto i = 1; i < tb_height() + 1; ++i) {
      printf("\e[%d;%dH", i, 0);
      printf("\e[K");
      fflush(stdout);
      eraseLineFrontBuffer(i - 1);
    }
  }

  void eraseLineFrontBuffer(int y) const {
    for(int i = 0; i < tb_width(); ++i) {
      tb_change_cell_front(i, y, ' ', 0, 0);
    }
  }

  void changeFileViewType() {
    if(fileViews_[currentFileView_] -> getViewType() == FileView::ViewType::DETAIL)
      fileViews_[currentFileView_] -> setViewType(FileView::ViewType::SIMPLE);
    else if(fileViews_[currentFileView_] -> getViewType() == FileView::ViewType::SIMPLE)
      fileViews_[currentFileView_] -> setViewType(FileView::ViewType::DETAIL);
  }

  bool getReadline(const std::string& prompt, std::string& buf,
                   const char* txt = 0, std::vector<std::string>* complist = 0,
                   std::list<std::string>* history = 0) const {
    printf("\e[%d;%dH\e[K", tb_height(), 0);
    printf("\e[27;22m");
    printf("\e[?25h"); // show cursor
    fflush(stdout);

    linenoise::clearHistory();
    if(history != 0) {
      for(auto line: *history) {
        linenoise::AddHistory(line.c_str());
      }
    }

    if(complist != 0) {
      linenoise::SetCompletionCallback([complist](const char*ch, std::vector<std::string>& comp) {
                                         for(auto line: *complist) {
                                           if(strncmp(line.c_str(), ch, strlen(ch)) == 0)
                                             comp.emplace_back(line);
                                         }
                                       });
    }
    auto result = linenoise::Readline(prompt.c_str(), buf, txt);

    printf("\e[?25l"); // hide cursor
    printf("\e[%d;%dH\e[K", tb_height(), 0);
    fflush(stdout);
    eraseLineFrontBuffer(tb_height() - 1);

    return result;
  }

  char getInput(const std::string& prompt) const {
    printf("\e[%d;%dH\e[K", tb_height(), 0);
    printf("\e[27;22m");
    printf("%s", prompt.c_str());
    fflush(stdout);

    struct tb_event ev;
    while(tb_poll_event(&ev)) {
      if(ev.type == TB_EVENT_KEY) {
        printf("\e[%d;%dH\e[K", tb_height(), 0);
        fflush(stdout);
        eraseLineFrontBuffer(tb_height() - 1);

        return ev.ch;
      }
    }

    return 0;
  }

  bool printInfoMessage(const std::string& msg) const {
    static std::chrono::system_clock::time_point start_clock;
    static bool show = false;
    static std::string txt;

    if(!msg.empty()) {
      show = true;
      txt = msg;
      start_clock = std::chrono::system_clock::now();
    }

    if(show) {
      auto clock = std::chrono::system_clock::now();
      auto msec = std::chrono::duration_cast<
        std::chrono::milliseconds>(clock - start_clock).count();

      if(msec > 200) {
        txt.clear();
        show = false;
      }

      if(show) {
        std::string clr;
        for(int i = 0; i < tb_width(); ++i)
          clr.push_back(' ');
        drawText(0, tb_height() - 1, clr);
      }

      drawText(0, tb_height() - 1, txt);
    }

    return show;
  }

  void printTask() const {
    std::string txt;

    if(fileOperation_.getTaskCount() != 0)
      txt = "  [" + std::to_string(fileOperation_.getTaskCount()) + "]";
    else txt = "     ";

    drawText(tb_width() - txt.length(), 0, txt);
  }

  void printCurrentPath(const std::string& path) const {
    std::string txt = "[";

    for(int i = 1; i < TAB_MAX + 1; ++i)
      txt += std::to_string(i);

    txt += "] ";
    drawText(0, 0, txt, 0);
    drawText(currentFileView_ + 1, 0, std::to_string(currentFileView_ + 1), TB_REVERSE);

    drawText(0 + txt.length(), 0, path, TB_CYAN | TB_BOLD);
  }

  int printSelectFilesCnt() const {
    auto selectFilesCnt = fileViews_[currentFileView_] -> getSelectFiles().size();
    if(selectFilesCnt != 0) {
      std::string msg = "[" + std::to_string(selectFilesCnt)+ "]";
      drawText(tb_width() - msg.length(), tb_height() - 2, msg);

      return msg.length();
    }

    return 0;
  }

  void printCurrentFileInfo() const {
    if(fileViews_[currentFileView_] -> getFileListCount() == 0) {
      printSelectFilesCnt();
      drawText(tb_width() - 3 - 1, tb_height() - 1, "0/0");
      return;
    }

    auto fileInfo = fileViews_[currentFileView_] -> getCurrentFileInfo();

    char buf[256];
    snprintf(buf, sizeof(buf), "%s %8.8s  %s",
             FileInfo::getModeStr(fileInfo).c_str(),
             FileInfo::getSizeStr(fileInfo).c_str(),
             FileInfo::getMTimeStr(fileInfo).c_str());

    drawText(0, tb_height() - 1, buf);

    snprintf(buf, sizeof(buf), "%d/%d",
             fileViews_[currentFileView_] -> getCursorPos() + 1,
             fileViews_[currentFileView_] -> getFileListCount());
    drawText(tb_width() - strlen(buf) - 1, tb_height() - 1, buf);

    drawText(0, tb_height() - 2, "[");
    int len = drawText(1, tb_height() - 2, fileInfo.getFileName(), TB_CYAN | TB_BOLD);

    if(len + 1 > tb_width())
      drawText(tb_width() - 1, tb_height() - 2, "]");
    else
      drawText(len + 1, tb_height() - 2, "]");

    int len2 = printSelectFilesCnt();
    if(len2 != 0) {
      if(len + 1 > tb_width() - len2)
        drawText(tb_width() - len2 - 1, tb_height() - 2, "]");
    }
  }

  void updateCmdCache() {
    if(getenv("PATH") == 0) return;

    std::istringstream stream(std::string(getenv("PATH")));
    std::string path;

    while (std::getline(stream, path, ':')) {
      auto dir = DirInfo(path);
      for(int i = 0; i < dir.getCount(); ++i) {
        auto fileInfo = dir.at(i);
        if(!fileInfo.isDir())
          cmdCache_.emplace_back(fileInfo.getFileName());
      }
    }
  }

  std::array<std::unique_ptr<FileView>, TAB_MAX> fileViews_;
  PreView preView_;
  FileOperation fileOperation_;

  std::vector<std::string> cmdCache_;
  std::list<std::string> filterHistory_;
  struct Buffer {
    FileOperation::Task::Operation operation;
    std::vector<std::string> selectedFiles;
  };
  Buffer buffer_;
  int currentFileView_;
  std::string tmpFileName_;
  PickerMode pickerMode_;
  std::string pickerOutput_;
};

int main(int argc, char **argv)
{
  cmdline::parser parser;

  parser.add<std::string>("choosefile", 0, "file chooser", false, "");
  parser.add<std::string>("choosefiles", 0, "multiple files", false, "");
  parser.add<std::string>("choosedir", 0, "directory chooser", false, "");
  parser.add("help", 0, "print this message");
  parser.footer("path");
  bool ok = parser.parse(argc, argv);

  if(parser.exist("help")){
    std::cerr << parser.usage();
    return 0;
  }

  if(!ok){
    std::cerr << parser.error() << std::endl << parser.usage();
    return 1;
  }

  NullListener lisner;
  TagLib::setDebugListener(&lisner);

  setlocale(LC_ALL, "");

  if(getenv("HOME") != 0) {
    config.LoadConfig(std::string(getenv("HOME")) + "/.config/Minase/config.ini");
    config.LoadBookmarks(std::string(getenv("HOME")) + "/.config/Minase/bookmarks");
    config.LoadPlugins(std::string(getenv("HOME")) + "/.config/Minase/plugin.ini");
  }
#ifdef USE_MIGEMO
  mgm = migemo_open(config.getMigemoDict().c_str());
  if(mgm == MIGEMO_DICTID_INVALID) {
    std::cerr << "migemo_open() failed." << std::endl;
    return 1;
  }

  migemo_setproc_int2char(mgm, (MIGEMO_PROC_INT2CHAR)(utf8pcre_int2char));
#endif

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);

  int init = tb_init();
  if(init) {
    std::cerr << "tb_init() failed with error code: " << init << std::endl;
    return 1;
  }
  tb_use_wcwidth_cjk(config.wcwidthCJK());
  tb_select_input_mode(TB_INPUT_ALT);
  atexit(tb_shutdown);

  std::string path;

  if(parser.rest().size() > 0) {
    path = parser.rest()[0];
    if(path.at(path.size() - 1) != '/') {
      path += "/";
    }

    struct stat lstat_;
    lstat(path.c_str(), &lstat_);
    if(!S_ISDIR(lstat_.st_mode)) path = "";
  }

  if(path == "") {
    char* currentPath = get_current_dir_name();
    path = std::string(currentPath) + "/";
    free(currentPath);
  }

  Minase::PickerMode mode = Minase::PICKER_NONE;
  std::string output;
  if (!parser.get<std::string>("choosefile").empty()) {
    mode = Minase::PICKER_FILE;
    output = parser.get<std::string>("choosefile");
  }
  if (!parser.get<std::string>("choosedir").empty()) {
    mode = Minase::PICKER_DIR;
    output = parser.get<std::string>("choosedir");
  }
  if (!parser.get<std::string>("choosefiles").empty()) {
    mode = Minase::PICKER_FILES;
    output = parser.get<std::string>("choosefiles");
  }

  Minase minase(path, mode, output);
  minase.run();

#ifdef USE_MIGEMO
  migemo_close(mgm);
#endif

  return 0;
}
