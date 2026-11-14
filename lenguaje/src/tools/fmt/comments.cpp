#include "comments.h"

#include <utility>

namespace lgfmt {

std::vector<Comment> collectComments(const std::string& src) {
  std::vector<Comment> out;
  const size_t n = src.size();
  size_t start = 0;
  int line = 1;
  while (start < n) {
    size_t end = src.find('\n', start);
    if (end == std::string::npos) end = n;
    bool inStr = false;
    bool code = false;  // ya hay algo distinto de blancos antes en esta línea
    for (size_t j = start; j < end; ++j) {
      const char c = src[j];
      if (inStr) {
        if (c == '\\' && j + 1 < end) {
          ++j;  // salta el carácter escapado
        } else if (c == '"') {
          inStr = false;
        }
        continue;
      }
      if (c == '"') {
        inStr = true;
        code = true;
        continue;
      }
      if (c == '#') {
        size_t e = end;
        while (e > j && (src[e - 1] == '\r' || src[e - 1] == ' ' || src[e - 1] == '\t')) --e;
        Comment cm;
        cm.line = line;
        cm.col = static_cast<int>(j - start) + 1;
        cm.trailing = code;
        cm.text = src.substr(j, e - j);
        out.push_back(std::move(cm));
        break;  // el resto de la línea es comentario
      }
      if (c != ' ' && c != '\t' && c != '\r') code = true;
    }
    start = end + 1;
    ++line;
  }
  return out;
}

std::vector<std::string> splitLines(const std::string& src) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < src.size()) {
    size_t end = src.find('\n', start);
    if (end == std::string::npos) end = src.size();
    std::string l = src.substr(start, end - start);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lines.push_back(std::move(l));
    start = end + 1;
  }
  return lines;
}

}  // namespace lgfmt
