// Implementación de TypeTable y Type (ver types.h, contrato).
#include "sema/types.h"

#include <sstream>
#include <utility>

namespace lg {
namespace {

// Clave de internado: dos tipos con la misma clave son el mismo objeto.
std::string keyOf(const Type& t) {
  std::ostringstream os;
  os << static_cast<int>(t.kind) << '|' << static_cast<const void*>(t.elem) << '|'
     << static_cast<const void*>(t.key) << '|' << static_cast<const void*>(t.val) << '|';
  for (int64_t d : t.shape) os << d << ',';
  os << '|' << t.name << '|';
  for (const Type* p : t.params) os << static_cast<const void*>(p) << ',';
  os << '|' << static_cast<const void*>(t.ret);
  return os.str();
}

std::string subStr(const Type* t) { return t ? t->str() : std::string("?"); }

}  // namespace

// ============================ Type ============================

bool Type::isHeap() const {
  switch (kind) {
    case TypeKind::Str:
    case TypeKind::List:
    case TypeKind::Dict:
    case TypeKind::Tensor:
    case TypeKind::Struct:
    case TypeKind::Option:
    case TypeKind::Func:
    case TypeKind::Hilo:
    case TypeKind::Canal:
    case TypeKind::Debil:
      return true;
    default:
      return false;
  }
}

// Para tipos que no son tensor se devuelve true (no tienen dimensiones desconocidas).
bool Type::shapeKnown() const {
  if (kind != TypeKind::Tensor) return true;
  for (int64_t d : shape) {
    if (d < 0) return false;
  }
  return true;
}

std::string Type::str() const {
  switch (kind) {
    case TypeKind::Error:
      return "error";
    case TypeKind::Void:
      return "void";
    case TypeKind::Int:
      return "int";
    case TypeKind::Float:
      return "float";
    case TypeKind::Bool:
      return "bool";
    case TypeKind::Str:
      return "str";
    case TypeKind::List:
      return "List[" + subStr(elem) + "]";
    case TypeKind::Dict:
      return "Dict[" + subStr(key) + ", " + subStr(val) + "]";
    case TypeKind::Tensor: {
      std::string s = "Tensor[" + subStr(elem) + ", (";
      for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) s += ", ";
        s += shape[i] < 0 ? std::string("?") : std::to_string(shape[i]);
      }
      if (shape.size() == 1) s += ",";
      s += ")]";
      return s;
    }
    case TypeKind::Struct:
      return name;
    case TypeKind::Option:
      return "Option[" + subStr(elem) + "]";
    case TypeKind::Hilo:
      return "Hilo[" + subStr(elem) + "]";
    case TypeKind::Canal:
      return "Canal[" + subStr(elem) + "]";
    case TypeKind::Debil:
      return "Debil[" + subStr(elem) + "]";
    case TypeKind::Func: {
      std::string s = "fn(";
      for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) s += ", ";
        s += subStr(params[i]);
      }
      s += ")";
      if (ret && ret->kind != TypeKind::Void) s += " -> " + ret->str();
      return s;
    }
  }
  return "?";
}

// ============================ TypeTable ============================

TypeTable::TypeTable() {
  Type t;
  error_ = intern(t);
  t.kind = TypeKind::Void;
  void_ = intern(t);
  t.kind = TypeKind::Int;
  int_ = intern(t);
  t.kind = TypeKind::Float;
  float_ = intern(t);
  t.kind = TypeKind::Bool;
  bool_ = intern(t);
  t.kind = TypeKind::Str;
  str_ = intern(t);
}

const Type* TypeTable::intern(Type t) {
  std::string key = keyOf(t);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  storage_.push_back(std::make_unique<Type>(std::move(t)));
  const Type* p = storage_.back().get();
  cache_.emplace(std::move(key), p);
  return p;
}

const Type* TypeTable::listOf(const Type* elem) {
  if (!elem || elem->kind == TypeKind::Error) return error_;
  Type t;
  t.kind = TypeKind::List;
  t.elem = elem;
  return intern(std::move(t));
}

const Type* TypeTable::dictOf(const Type* key, const Type* val) {
  if (!key || !val || key->kind == TypeKind::Error || val->kind == TypeKind::Error) return error_;
  Type t;
  t.kind = TypeKind::Dict;
  t.key = key;
  t.val = val;
  return intern(std::move(t));
}

const Type* TypeTable::tensorOf(const Type* elem, const std::vector<int64_t>& shape) {
  if (elem != int_ && elem != float_) return error_;
  Type t;
  t.kind = TypeKind::Tensor;
  t.elem = elem;
  t.shape = shape;
  return intern(std::move(t));
}

const Type* TypeTable::structOf(const std::string& name) {
  Type t;
  t.kind = TypeKind::Struct;
  t.name = name;
  return intern(std::move(t));
}

const Type* TypeTable::optionOf(const Type* elem) {
  if (!elem || elem->kind == TypeKind::Error || elem->kind == TypeKind::Void) return error_;
  Type t;
  t.kind = TypeKind::Option;
  t.elem = elem;
  return intern(std::move(t));
}

const Type* TypeTable::hiloOf(const Type* elem) {
  if (!elem || elem->kind == TypeKind::Error || elem->kind == TypeKind::Void) return error_;
  Type t;
  t.kind = TypeKind::Hilo;
  t.elem = elem;
  return intern(std::move(t));
}

const Type* TypeTable::canalOf(const Type* elem) {
  if (!elem || elem->kind == TypeKind::Error || elem->kind == TypeKind::Void) return error_;
  Type t;
  t.kind = TypeKind::Canal;
  t.elem = elem;
  return intern(std::move(t));
}

const Type* TypeTable::debilOf(const Type* elem) {
  if (!elem || elem->kind == TypeKind::Error || elem->kind == TypeKind::Void) return error_;
  Type t;
  t.kind = TypeKind::Debil;
  t.elem = elem;
  return intern(std::move(t));
}

const Type* TypeTable::funcOf(const std::vector<const Type*>& params, const Type* ret) {
  if (!ret) return error_;
  for (const Type* p : params) {
    if (!p || p->kind == TypeKind::Error || p->kind == TypeKind::Void) return error_;
  }
  Type t;
  t.kind = TypeKind::Func;
  t.params = params;
  t.ret = ret;
  return intern(std::move(t));
}

// Iguales, o tensores del mismo elemento y rango cuyas dimensiones coinciden
// salvo que alguna sea -1 (desconocida) en cualquiera de los dos.
bool TypeTable::compatible(const Type* expected, const Type* actual) {
  if (!expected || !actual) return false;
  if (expected == actual) return true;
  if (expected->kind != TypeKind::Tensor || actual->kind != TypeKind::Tensor) return false;
  if (expected->elem != actual->elem) return false;
  if (expected->shape.size() != actual->shape.size()) return false;
  for (size_t i = 0; i < expected->shape.size(); ++i) {
    int64_t a = expected->shape[i];
    int64_t b = actual->shape[i];
    if (a >= 0 && b >= 0 && a != b) return false;
  }
  return true;
}

}  // namespace lg
