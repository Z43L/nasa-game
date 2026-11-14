#include "common/diagnostics.h"

#include <sstream>

namespace lg {

void Diagnostics::registerSource(const std::string& file, const std::string& text) {
  sources_[file] = text;
}

void Diagnostics::error(const Loc& loc, std::string message, std::string hint) {
  items_.push_back({Diagnostic::Severity::Error, loc, std::move(message), std::move(hint)});
}

void Diagnostics::warning(const Loc& loc, std::string message, std::string hint) {
  items_.push_back({Diagnostic::Severity::Warning, loc, std::move(message), std::move(hint)});
}

bool Diagnostics::hasErrors() const { return errorCount() > 0; }

size_t Diagnostics::errorCount() const {
  size_t n = 0;
  for (const auto& d : items_)
    if (d.severity == Diagnostic::Severity::Error) ++n;
  return n;
}

const std::vector<Diagnostic>& Diagnostics::all() const { return items_; }

static std::string sourceLine(const std::string& text, int line) {
  std::istringstream in(text);
  std::string s;
  for (int i = 1; std::getline(in, s); ++i)
    if (i == line) return s;
  return "";
}

void Diagnostics::print(std::ostream& os) const {
  // Como máximo kMaxPrinted errores; el resto se cuenta pero no se imprime (evita cascadas enormes).
  constexpr size_t kMaxPrinted = 20;
  size_t printedErrors = 0;
  for (size_t i = 0; i < items_.size(); ++i) {
    const Diagnostic& d = items_[i];
    if (d.severity == Diagnostic::Severity::Error) {
      if (printedErrors == kMaxPrinted) {
        size_t omitted = 0;
        for (size_t j = i; j < items_.size(); ++j)
          if (items_[j].severity == Diagnostic::Severity::Error) ++omitted;
        os << "demasiados errores, se omiten " << omitted << " más\n";
        return;
      }
      ++printedErrors;
    }
    const char* sev = d.severity == Diagnostic::Severity::Error     ? "error"
                      : d.severity == Diagnostic::Severity::Warning ? "aviso"
                                                                    : "nota";
    os << d.loc.file << ':' << d.loc.line << ':' << d.loc.col << ": " << sev << ": "
       << d.message << '\n';
    auto it = sources_.find(d.loc.file);
    if (it != sources_.end() && d.loc.line > 0) {
      std::string line = sourceLine(it->second, d.loc.line);
      std::string num = std::to_string(d.loc.line);
      os << "   " << num << " | " << line << '\n';
      os << "   " << std::string(num.size(), ' ') << " | "
         << std::string(d.loc.col > 0 ? d.loc.col - 1 : 0, ' ') << "^\n";
    }
    if (!d.hint.empty()) os << "sugerencia: " << d.hint << '\n';
  }
}

}  // namespace lg
