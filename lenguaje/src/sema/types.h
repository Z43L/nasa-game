// CONTRATO (propietario: integrador). Representación de tipos del lenguaje.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lg {

enum class TypeKind { Error, Void, Int, Float, Bool, Str, List, Dict, Tensor, Struct, Option, Func,
                      Hilo, Canal, Debil };  // Hilo[T], Canal[T], Debil[T] (v2, concurrencia)

// Todos los tipos viven en una TypeTable y se INTERNAN: dos tipos iguales son el mismo puntero.
// Excepción: Tensor con dimensiones -1 (desconocidas) se compara con TypeTable::compatible.
struct Type {
  TypeKind kind = TypeKind::Error;
  const Type* elem = nullptr;     // List: elemento. Tensor: Int o Float. Option: tipo de Some. Hilo/Canal/Debil: T.
  const Type* key = nullptr;      // Dict: tipo de clave.
  const Type* val = nullptr;      // Dict: tipo de valor.
  std::vector<int64_t> shape;     // Tensor: forma. -1 = dimensión conocida solo en ejecución.
  std::string name;               // Struct: nombre de la struct.
  std::vector<const Type*> params;  // Func: tipos de los parámetros.
  const Type* ret = nullptr;      // Func: tipo de retorno (void si no devuelve valor).

  // true si el valor vive en el heap y se gestiona con conteo de referencias:
  // Str, List, Dict, Tensor, Struct, Option (caja con etiqueta y valor), Func (clausura),
  // Hilo, Canal y Debil (objetos del runtime).
  // Int/Float/Bool son valores directos.
  bool isHeap() const;
  // true si todas las dimensiones de un tensor son conocidas en compilación.
  bool shapeKnown() const;
  // Texto legible: "int", "List[int]", "Dict[str, int]", "Tensor[float, (2, 3)]", "Punto",
  // "Option[int]", "fn(int, str) -> bool".
  std::string str() const;
};

class TypeTable {
public:
  TypeTable();

  const Type* error() const { return error_; }
  const Type* voidT() const { return void_; }
  const Type* intT() const { return int_; }
  const Type* floatT() const { return float_; }
  const Type* boolT() const { return bool_; }
  const Type* strT() const { return str_; }

  const Type* listOf(const Type* elem);
  const Type* dictOf(const Type* key, const Type* val);
  // elem debe ser intT() o floatT().
  const Type* tensorOf(const Type* elem, const std::vector<int64_t>& shape);
  const Type* structOf(const std::string& name);
  const Type* optionOf(const Type* elem);
  // Tipos de concurrencia. El argumento debe ser un tipo válido (no void ni error).
  const Type* hiloOf(const Type* elem);
  const Type* canalOf(const Type* elem);
  const Type* debilOf(const Type* elem);
  // Tipo de función. ret == voidT() para una función sin retorno.
  const Type* funcOf(const std::vector<const Type*>& params, const Type* ret);

  // ¿Un valor de tipo `actual` puede usarse donde se espera `expected`?
  // Iguales, o tensores cuyas formas coinciden salvo dimensiones -1 en cualquiera de los dos.
  static bool compatible(const Type* expected, const Type* actual);

private:
  const Type* intern(Type t);

  std::deque<std::unique_ptr<Type>> storage_;
  std::map<std::string, const Type*> cache_;
  const Type* error_;
  const Type* void_;
  const Type* int_;
  const Type* float_;
  const Type* bool_;
  const Type* str_;
};

}  // namespace lg
