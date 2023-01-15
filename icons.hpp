#ifndef ICONS_HPP
#define ICONS_HPP

struct Icon {
  const char* match;
  const char* icon;
};

static const Icon dirIcon = {"", ""};
static const Icon fileIcon = {"", ""};
static const Icon exeIcon = {"", ""};

static const Icon icons[] = {
  // video
  {"mp4", ""},
  {"flv", ""},
  {"mp4", ""},
  {"mov", ""},
  {"wma", ""},
  {"webm", ""},
  {"avi", ""},
  {"vid", ""},
  {"mpg", ""},
  {"m4v", ""},
  {"wmv", ""},
  {"mkv", ""},
  {"mpeg", ""},

  // image
  {"jpg", ""},
  {"jpeg", ""},
  {"ico", ""},
  {"svg", ""},
  {"gif", ""},
  {"bmp", ""},
  {"xcf", ""},
  {"png", ""},
  {"psd", ""},
  {"webp", ""},

  // audio
  {"wav", ""},
  {"opus", ""},
  {"mp3", ""},
  {"flac", ""},
  {"m4a", ""},
  {"ogg", ""},
  {"oga", ""},
  {"aup", ""},
  {"ape", ""},
  {"tta", ""},

  // archive
  {"zip", ""},
  {"xz", ""},
  {"gzip", ""},
  {"cab", ""},
  {"tgz", ""},
  {"lzh", ""},
  {"lha", ""},
  {"tar", ""},
  {"apk", ""},
  {"rar", ""},
  {"7z", ""},
  {"cpio", ""},
  {"bz2", ""},
  {"lzma", ""},
  {"zst", ""},
  {"gz", ""},
  {"cbz", ""},
  {"cbr", ""},
  {"iso", ""},
  {"img", ""},

  // bin
  {"o", ""},
  {"class", ""},
  {"so", ""},
  {"a", ""},
  {"bin", ""},
  {"elf", ""},
  {"dll", ""},
  {"exe", ""},

  // text
  {"txt", ""},
  {"md", ""},
  {"markdown", ""},

  {"html", ""},
  {"htm", ""},
  {"xhtml", ""},
  {"rss", ""},
  {"css", ""},

  {"pdf", ""},
  {"doc", ""},
  {"docx", ""},
  {"rtf", ""},

  {"yaml", ""},
  {"toml", ""},
  {"yml", ""},
  {"tml", ""},
  {"ini", ""},

  {"json", ""},

  {"1", ""},

  // code
  {"sh", ""},
  {"cmake", ""},
  {"mk", ""},

  {"c", ""},
  {"h", ""},
  {"cpp", ""},
  {"c++", ""},
  {"cxx", ""},
  {"cc", ""},
  {"hpp", ""},
  {"hh", ""},
  {"hxx", ""},
  {"cs", ""},

  {"py", ""},
  {"pyo", ""},
  {"pyd", ""},
  {"pyc", ""},

  {"rb", ""},
  {"gem", ""},
  {"rake", ""},

  {"pl", ""},
  {"pm", ""},
  {"t", ""},

  {"go", ""},

  {"rs", ""},

  {"js", ""},
  {"ejs", ""},
  {"ts", ""},

  {"java", ""},
  {"jar", ""},

  {"php", ""},

  {"vim", ""},
  {"vimrc", ""},

  {"diff", ""},
  {"patch", ""},
};

#endif
