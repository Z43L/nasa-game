// Información de depuración DWARF. Ver debug_info.h.

#include "codegen/debug_info.h"

#include <filesystem>

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/TargetParser/Triple.h"

namespace L = llvm;
namespace fs = std::filesystem;

namespace lg {

DebugInfo::DebugInfo(L::Module& mod, const Program& prog)
    : mod_(mod), ctx_(mod.getContext()) {
  for (const auto& d : prog.decls) {
    if (d->kind != DeclKind::Struct) continue;
    const auto& sd = static_cast<const StructDecl&>(*d);
    structs_[sd.name] = &sd;
  }
  const bool darwin = mod.getTargetTriple().isOSDarwin();
  mod.addModuleFlag(L::Module::Warning, "Debug Info Version", L::DEBUG_METADATA_VERSION);
  // macOS (lldb con dsymutil y objetos) trabaja mejor con DWARF 4; en el resto, DWARF 5.
  mod.addModuleFlag(L::Module::Warning, "Dwarf Version", darwin ? 4 : 5);
  dbg_ = std::make_unique<L::DIBuilder>(mod);
  cu_ = dbg_->createCompileUnit(L::dwarf::DW_LANG_C99, fileFor(prog.file), "lgc", false, "", 0);
}

DebugInfo::~DebugInfo() { finalize(); }

void DebugInfo::finalize() {
  if (finalized_) return;
  finalized_ = true;
  dbg_->finalize();
}

// ------------------------------------------------------------------ archivos y ámbitos

L::DIFile* DebugInfo::fileFor(const std::string& path) {
  auto it = files_.find(path);
  if (it != files_.end()) return it->second;
  fs::path p(path);
  std::error_code ec;
  fs::path abs = fs::absolute(p, ec);
  if (ec) abs = p;
  L::DIFile* f = dbg_->createFile(abs.filename().string(), abs.parent_path().string());
  files_[path] = f;
  return f;
}

// Ámbito para una ubicación dentro de la función actual. Si la línea viene de otro archivo
// (un import), se usa un bloque léxico con ese archivo para que lldb muestre la línea correcta.
L::DIScope* DebugInfo::scopeFor(const Loc& loc) {
  L::DISubprogram* sp = stack_.back().sp;
  L::DIFile* f = fileFor(loc.file);
  if (f == sp->getFile()) return sp;
  return dbg_->createLexicalBlockFile(sp, f, 0);
}

void DebugInfo::beginFunction(L::Function* F, const std::string& name, const std::string& linkageName,
                              const FnDecl* fd, const Loc& loc) {
  L::DIFile* file = fileFor(loc.file);
  unsigned line = loc.line > 0 ? static_cast<unsigned>(loc.line) : 1u;
  L::DISubroutineType* ty =
      fd ? signatureOf(fd, fd->retType) : dbg_->createSubroutineType(dbg_->getOrCreateTypeArray({}));
  L::DISubprogram* sp = dbg_->createFunction(file, name, linkageName, file, line, ty, line,
                                             L::DINode::FlagPrototyped, L::DISubprogram::SPFlagDefinition);
  F->setSubprogram(sp);
  stack_.push_back(FnState{sp, 0});
}

void DebugInfo::endFunction() { stack_.pop_back(); }

// ------------------------------------------------------------------ ubicaciones

void DebugInfo::setLoc(L::IRBuilder<>& B, const Loc& loc) {
  if (loc.line <= 0 || stack_.empty() || !stack_.back().sp) {
    B.SetCurrentDebugLocation(L::DebugLoc());
    return;
  }
  unsigned col = loc.col > 0 ? static_cast<unsigned>(loc.col) : 1u;
  L::DILocation* dl = L::DILocation::get(ctx_, static_cast<unsigned>(loc.line), col, scopeFor(loc));
  B.SetCurrentDebugLocation(L::DebugLoc(dl));
}

void DebugInfo::declareVariable(L::IRBuilder<>& B, const std::string& name, const Type* t, L::Value* addr,
                                bool isParam) {
  if (stack_.empty() || name.empty() || !t || !addr || !B.GetInsertBlock()) return;
  const L::DILocation* cur = B.getCurrentDebugLocation().get();
  if (!cur) return;
  L::DIScope* scope = cur->getScope();
  L::DIFile* file = scope->getFile();
  unsigned line = cur->getLine();
  L::DIType* ty = typeOf(t);
  L::DILocalVariable* var;
  if (isParam) {
    var = dbg_->createParameterVariable(scope, name, ++stack_.back().argNo, file, line, ty, true);
  } else {
    var = dbg_->createAutoVariable(scope, name, file, line, ty, true);
  }
  dbg_->insertDeclare(addr, var, dbg_->createExpression(), cur, B.GetInsertBlock());
}

// ------------------------------------------------------------------ tipos

L::DIBasicType* DebugInfo::basic(const std::string& name, uint64_t bits, unsigned enc) {
  return dbg_->createBasicType(name, bits, enc);
}

L::DISubroutineType* DebugInfo::signatureOf(const FnDecl* fd, const Type* retType) {
  std::vector<L::Metadata*> tys;
  // El primer elemento es el retorno; nullptr significa void.
  tys.push_back(retType && retType->kind != TypeKind::Void ? typeOf(retType) : nullptr);
  for (const auto& p : fd->params) tys.push_back(typeOf(p.type));
  return dbg_->createSubroutineType(dbg_->getOrCreateTypeArray(tys));
}

// Tipo visible de un valor. Los valores heap se ven como puntero a su tipo base.
L::DIType* DebugInfo::typeOf(const Type* t) {
  if (!t) return nullptr;
  auto it = types_.find(t);
  if (it != types_.end()) return it->second;
  L::DIType* r = nullptr;
  switch (t->kind) {
    case TypeKind::Int: r = basic("int", 64, L::dwarf::DW_ATE_signed); break;
    case TypeKind::Float: r = basic("float", 64, L::dwarf::DW_ATE_float); break;
    case TypeKind::Bool: r = basic("bool", 8, L::dwarf::DW_ATE_boolean); break;
    case TypeKind::Void:
    case TypeKind::Error: return nullptr;
    default:
      // Referencia recursiva (el tipo se está describiendo): puntero a void, sin cachear.
      if (inProgress_.count(t)) return dbg_->createPointerType(nullptr, 64);
      r = dbg_->createPointerType(baseOf(t), 64);
      break;
  }
  types_[t] = r;
  return r;
}

// Tipo base de un valor heap (lo que apunta el puntero). Los miembros se describen con un
// DICompositeType ya completo (no temporal): así no hace falta replaceArrays ni resolver nodos.
// Un miembro cuyo tipo está en curso (struct que se contiene a sí misma por Option, ...) se muestra
// como puntero a void.
L::DICompositeType* DebugInfo::baseOf(const Type* t) {
  auto it = bases_.find(t);
  if (it != bases_.end()) return it->second;
  inProgress_.insert(t);

  L::DIFile* file = cu_->getFile();
  const L::DataLayout& DL = mod_.getDataLayout();
  L::DINodeArray noElems = dbg_->getOrCreateArray({});
  L::DICompositeType* c = nullptr;

  auto align = [](L::Type* ty, const L::DataLayout& dl) {
    return static_cast<uint32_t>(dl.getABITypeAlign(ty).value() * 8);
  };

  if (t->kind == TypeKind::Struct) {
    auto sit = structs_.find(t->name);
    L::StructType* sty = L::StructType::getTypeByName(ctx_, "struct." + t->name);
    if (sit != structs_.end() && sty) {
      const StructDecl& sd = *sit->second;
      file = fileFor(sd.loc.file);
      const L::StructLayout* sl = DL.getStructLayout(sty);
      // Los campos son los últimos elementos del tipo LLVM; delante va la cabecera (contador, destroy...).
      const unsigned hdr = sty->getNumElements() - static_cast<unsigned>(sd.fields.size());
      std::vector<L::Metadata*> elems;
      for (size_t j = 0; j < sd.fields.size(); ++j) {
        const FieldDecl& f = sd.fields[j];
        const unsigned idx = hdr + static_cast<unsigned>(j);
        L::Type* et = sty->getElementType(idx);
        elems.push_back(dbg_->createMemberType(
            file, f.name, file, static_cast<unsigned>(f.loc.line), DL.getTypeSizeInBits(et).getFixedValue(),
            align(et, DL), sl->getElementOffsetInBits(idx), L::DINode::FlagZero, typeOf(f.type)));
      }
      c = dbg_->createStructType(file, t->name, file, static_cast<unsigned>(sd.loc.line),
                                 DL.getTypeSizeInBits(sty).getFixedValue(), align(sty, DL),
                                 L::DINode::FlagZero, static_cast<L::DIType*>(nullptr),
                                 dbg_->getOrCreateArray(elems));
    }
  } else if (t->kind == TypeKind::Option && optionBox_) {
    // Caja heap de codegen: ..., etiqueta (penúltimo elemento), valor (último).
    L::StructType* box = optionBox_(t);
    const L::StructLayout* sl = DL.getStructLayout(box);
    const unsigned n = box->getNumElements();
    L::Type* tt = box->getElementType(n - 2);
    L::Type* vt = box->getElementType(n - 1);
    std::vector<L::Metadata*> elems = {
        dbg_->createMemberType(file, "etiqueta", file, 0, DL.getTypeSizeInBits(tt).getFixedValue(), align(tt, DL),
                               sl->getElementOffsetInBits(n - 2), L::DINode::FlagZero,
                               basic("int", 64, L::dwarf::DW_ATE_signed)),
        dbg_->createMemberType(file, "valor", file, 0, DL.getTypeSizeInBits(vt).getFixedValue(), align(vt, DL),
                               sl->getElementOffsetInBits(n - 1), L::DINode::FlagZero, typeOf(t->elem)),
    };
    c = dbg_->createStructType(file, t->str(), file, 0, DL.getTypeSizeInBits(box).getFixedValue(),
                               align(box, DL), L::DINode::FlagZero, static_cast<L::DIType*>(nullptr),
                               dbg_->getOrCreateArray(elems));
  }
  // Clausuras, Str, List, Dict, Tensor y structs sin declaración conocida: tipo con nombre y sin campos.
  if (!c) {
    c = dbg_->createStructType(file, t->str(), file, 0, uint64_t{0}, uint32_t{0}, L::DINode::FlagZero,
                               static_cast<L::DIType*>(nullptr), noElems);
  }
  inProgress_.erase(t);
  bases_[t] = c;
  return c;
}

}  // namespace lg
