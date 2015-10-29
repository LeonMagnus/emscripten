//
// WebAssembly representation and processing library
//

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "colors.h"

namespace wasm {

// Utilities

// Arena allocation for mixed-type data.
struct Arena {
  std::vector<char*> chunks;
  int index; // in last chunk

  template<class T>
  T* alloc() {
    const size_t CHUNK = 10000;
    size_t currSize = (sizeof(T) + 7) & (-8); // same alignment as malloc TODO optimize?
    assert(currSize < CHUNK);
    if (chunks.size() == 0 || index + currSize >= CHUNK) {
      chunks.push_back(new char[CHUNK]);
      index = 0;
    }
    T* ret = (T*)(chunks.back() + index);
    index += currSize;
    new (ret) T();
    return ret;
  }

  void clear() {
    for (char* chunk : chunks) {
      delete[] chunk;
    }
    chunks.clear();
  }

  ~Arena() {
    clear();
  }
};

std::ostream &doIndent(std::ostream &o, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) {
    o << "  ";
  }
  return o;
}
void incIndent(std::ostream &o, unsigned& indent) {
  o << '\n';
  indent++;    
}
void decIndent(std::ostream &o, unsigned& indent) {
  indent--;
  doIndent(o, indent);
  o << ')';
}

// Basics

struct Name : public cashew::IString {
  Name() : cashew::IString() {}
  Name(const char *str) : cashew::IString(str) {}
  Name(cashew::IString str) : cashew::IString(str) {}

  std::ostream& print(std::ostream &o) {
    assert(str);
    o << str;
    return o;
  }
};

// Types

enum BasicType {
  none,
  i32,
  i64,
  f32,
  f64
};

std::ostream& printBasicType(std::ostream &o, BasicType type) {
  switch (type) {
    case BasicType::none: o << "none"; break;
    case BasicType::i32: o << "i32"; break;
    case BasicType::i64: o << "i64"; break;
    case BasicType::f32: o << "f32"; break;
    case BasicType::f64: o << "f64"; break;
  }
  return o;
}

unsigned getBasicTypeSize(BasicType type) {
  switch (type) {
    case BasicType::none: abort();
    case BasicType::i32: return 4;
    case BasicType::i64: return 8;
    case BasicType::f32: return 4;
    case BasicType::f64: return 8;
  }
}

bool isFloat(BasicType type) {
  switch (type) {
    case f32:
    case f64: return true;
  }
  return false;
}

BasicType getBasicType(unsigned size, bool float_) {
  if (size < 4) return BasicType::i32;
  if (size == 4) return float_ ? BasicType::f32 : BasicType::i32;
  if (size == 8) return float_ ? BasicType::f64 : BasicType::i64;
  abort();
}

struct Literal {
  BasicType type;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };

  Literal() : type(BasicType::none) {}
  Literal(int32_t init) : type(BasicType::i32), i32(init) {}
  Literal(int64_t init) : type(BasicType::i64), i64(init) {}
  Literal(float   init) : type(BasicType::f32), f32(init) {}
  Literal(double  init) : type(BasicType::f64), f64(init) {}

  std::ostream& print(std::ostream &o) {
    o << '(';
    printBasicType(o, type) << ".const ";
    switch (type) {
      case none: abort();
      case BasicType::i32: o << i32; break;
      case BasicType::i64: o << i64; break;
      case BasicType::f32: o << f32; break;
      case BasicType::f64: o << f64; break;
    }
    o << ')';
    return o;
  }
};

// Operators

enum UnaryOp {
  Clz, Ctz, Popcnt, // int
  Neg, Abs, Ceil, Floor, Trunc, Nearest, Sqrt // float
};

enum BinaryOp {
  Add, Sub, Mul, // int or float
  DivS, DivU, RemS, RemU, And, Or, Xor, Shl, ShrU, ShrS, // int
  Div, CopySign, Min, Max // float
};

enum RelationalOp {
  Eq, Ne, // int or float
  LtS, LtU, LeS, LeU, GtS, GtU, GeS, GeU, // int
  Lt, Le, Gt, Ge // float
};

enum ConvertOp {
  ExtendSInt32, ExtendUInt32, WrapInt64, TruncSFloat32, TruncUFloat32, TruncSFloat64, TruncUFloat64, ReinterpretFloat, // int
  ConvertSInt32, ConvertUInt32, ConvertSInt64, ConvertUInt64, PromoteFloat32, DemoteFloat64, ReinterpretInt // float
};

enum HostOp {
  PageSize, MemorySize, GrowMemory, HasFeature
};

// Expressions

class Expression {
public:
  BasicType type;

  Expression() : type(type) {}

  virtual std::ostream& print(std::ostream &o, unsigned indent) = 0;

  template<class T>
  bool is() {
    return !!dynamic_cast<T*>(this);
  }
};

std::ostream& printFullLine(std::ostream &o, unsigned indent, Expression *expression) {
  doIndent(o, indent);
  expression->print(o, indent);
  o << '\n';
}

std::ostream& printOpening(std::ostream &o, const char *str) {
  o << '(';
  Colors::red(o);
  o << str;
  Colors::normal(o);
  return o;
}

std::ostream& printMinorOpening(std::ostream &o, const char *str) {
  o << '(';
  Colors::orange(o);
  o << str;
  Colors::normal(o);
  return o;
}

typedef std::vector<Expression*> ExpressionList; // TODO: optimize  

class Nop : public Expression {
  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << "nop";
    return o;
  }
};

class Block : public Expression {
public:
  Name var;
  ExpressionList list;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "block");
    if (var.is()) {
      o << " ";
      var.print(o);
    }
    incIndent(o, indent);
    for (auto expression : list) {
      printFullLine(o, indent, expression);
    }
    decIndent(o, indent);
    return o;
  }
};

class If : public Expression {
public:
  Expression *condition, *ifTrue, *ifFalse;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "if");
    incIndent(o, indent);
    printFullLine(o, indent, condition);
    printFullLine(o, indent, ifTrue);
    if (ifFalse) printFullLine(o, indent, ifFalse);
    decIndent(o, indent);
    return o;
  }
};

class Loop : public Expression {
public:
  Name out, in;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "loop");
    if (out.is()) {
      o << " ";
      out.print(o);
      if (in.is()) {
        o << " ";
        in.print(o);
      }
    }
    incIndent(o, indent);
    printFullLine(o, indent, body);
    decIndent(o, indent);
    return o;
  }
};

class Label : public Expression {
public:
  Name var;
};

class Break : public Expression {
public:
  Name var;
  Expression *condition, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "break ");
    var.print(o);
    incIndent(o, indent);
    if (condition) printFullLine(o, indent, condition);
    if (value) printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Switch : public Expression {
public:
  struct Case {
    Literal value;
    Expression *body;
    bool fallthru;
  };

  Name var;
  Expression *value;
  std::vector<Case> cases;
  Expression *default_;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "switch ");
    var.print(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    o << "TODO: cases/default\n";
    decIndent(o, indent);
    return o;
  }

};

class Call : public Expression {
public:
  Name target;
  ExpressionList operands;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "call ");
    target.print(o);
    if (operands.size() > 0) {
      incIndent(o, indent);
      for (auto operand : operands) {
        printFullLine(o, indent, operand);
      }
      decIndent(o, indent);
    } else {
      o << ')';
    }
    return o;
  }
};

class CallImport : public Call {
};

class CallIndirect : public Expression {
public:
  Expression *target;
  ExpressionList operands;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "callindirect ");
    incIndent(o, indent);
    printFullLine(o, indent, target);
    for (auto operand : operands) {
      printFullLine(o, indent, operand);
    }
    decIndent(o, indent);
    return o;
  }
};

class GetLocal : public Expression {
public:
  Name id;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "get_local ");
    id.print(o) << ')';
    return o;
  }
};

class SetLocal : public Expression {
public:
  Name id;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "set_local ");
    id.print(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Load : public Expression {
public:
  unsigned bytes;
  bool signed_;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    printBasicType(o, getBasicType(bytes, float_)) << ".load";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
      if (!signed_) o << "_u";
    }
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    decIndent(o, indent);
    return o;
  }
};

class Store : public Expression {
public:
  unsigned bytes;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr, *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {

    o << '(';
    printBasicType(o, getBasicType(bytes, float_)) << ".store";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
    }
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Const : public Expression {
public:
  Literal value;

  Const* set(Literal value_) {
    value = value_;
    return this;
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    value.print(o);
    return o;
  }
};

class Unary : public Expression {
public:
  UnaryOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "unary ");
    switch (op) {
      case Neg: o << "neg"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Binary : public Expression {
public:
  BinaryOp op;
  Expression *left, *right;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    printBasicType(o, type) << '.';
    switch (op) {
      case Add:      o << "add"; break;
      case Sub:      o << "sub"; break;
      case Mul:      o << "mul"; break;
      case DivS:     o << "div_s"; break;
      case DivU:     o << "div_u"; break;
      case RemS:     o << "rem_s"; break;
      case RemU:     o << "rem_u"; break;
      case And:      o << "and"; break;
      case Or:       o << "or"; break;
      case Xor:      o << "xor"; break;
      case Shl:      o << "shl"; break;
      case ShrU:     o << "shr_u"; break;
      case ShrS:     o << "shr_s"; break;
      case Div:      o << "div"; break;
      case CopySign: o << "copysign"; break;
      case Min:      o << "min"; break;
      case Max:      o << "max"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    decIndent(o, indent);
    return o;
  }
};

class Compare : public Expression {
public:
  RelationalOp op;
  Expression *left, *right;

  Compare() {
    type = BasicType::i32;
  }

  std::ostream& print(std::ostream &o, unsigned indent) override {
    o << '(';
    printBasicType(o, type) << '.';
    switch (op) {
      case Eq:  o << "eq"; break;
      case Ne:  o << "ne"; break;
      case LtS: o << "lt_s"; break;
      case LtU: o << "lt_u"; break;
      case LeS: o << "le_s"; break;
      case LeU: o << "le_u"; break;
      case GtS: o << "gt_s"; break;
      case GtU: o << "gt_u"; break;
      case GeS: o << "ge_s"; break;
      case GeU: o << "ge_u"; break;
      case Lt:  o << "lt"; break;
      case Le:  o << "le"; break;
      case Gt:  o << "gt"; break;
      case Ge:  o << "ge"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    decIndent(o, indent);
    return o;
  }
};

class Convert : public Expression {
public:
  ConvertOp op;
  Expression *value;

  std::ostream& print(std::ostream &o, unsigned indent) override {
    printOpening(o, "convert ");
    switch (op) {
      case ConvertUInt32: o << "uint32toDouble"; break;
      case ConvertSInt32: o << "sint32toDouble"; break;
      case TruncSFloat64: o << "float64tosint32"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    decIndent(o, indent);
    return o;
  }
};

class Host : public Expression {
public:
  HostOp op;
  ExpressionList operands;
};

// Globals

struct NameType {
  Name name;
  BasicType type;
  NameType() : name(nullptr), type(none) {}
  NameType(Name name, BasicType type) : name(name), type(type) {}
};

class FunctionType {
public:
  Name name;
  BasicType result;
  std::vector<BasicType> params;

  std::ostream& print(std::ostream &o, unsigned indent) {
    if (params.size() > 0) {
      printMinorOpening(o, "param");
      for (auto& param : params) {
        o << ' ';
        printBasicType(o, param);
      }
      o << ')';
    }
    if (result != none) {
      if (params.size() > 0) o << ' ';
      printMinorOpening(o, "result ");
      printBasicType(o, result) << ')';
    }
    return o;
  }

  bool operator==(FunctionType& b) {
    if (name != b.name) return false; // XXX
    if (result != b.result) return false;
    if (params.size() != b.params.size()) return false;
    for (size_t i = 0; i < params.size(); i++) {
      if (params[i] != b.params[i]) return false;
    }
    return true;
  }
  bool operator!=(FunctionType& b) {
    return !(*this == b);
  }
};

class Function {
public:
  Name name;
  BasicType result;
  std::vector<NameType> params;
  std::vector<NameType> locals;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "func ") << name.str;
    if (params.size() > 0) {
      for (auto& param : params) {
        printMinorOpening(o, " (param ") << param.name.str << " ";
        printBasicType(o, param.type) << ")";
      }
    }
    if (result != none) {
      o << ' ';
      printMinorOpening(o, "result ");
      printBasicType(o, result) << ")";
    }
    incIndent(o, indent);
    for (auto& local : locals) {
      doIndent(o, indent);
      printMinorOpening(o, "local ") << local.name.str << " ";
      printBasicType(o, local.type) << ")\n";
    }
    printFullLine(o, indent, body);
    decIndent(o, indent);
    return o;
  }
};

class Import {
public:
  Name name, module, base; // name = module.base
  FunctionType type;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "import ") << name.str << " \"" << module.str << "\" \"" << base.str << "\" ";
    type.print(o, indent);
    o << ')';
    return o;
  }
};

class Export {
public:
  Name name;
  Name value;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "export") << " \"" << name.str << "\" ";
    value.print(o);
    o << ')';
    return o;
  }
};

class Table {
public:
  std::vector<Name> vars;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "table");
    for (auto var : vars) {
      o << ' ';
      var.print(o);
    }
    o << ')';
    return o;
  }
};

class Module {
protected:
  // wasm contents
  std::vector<FunctionType> functionTypes;
  std::map<Name, Import> imports;
  std::vector<Export> exports;
  Table table;
  std::vector<Function*> functions;

  // internals
  std::map<Name, void*> map; // maps var ids/names to things
  unsigned nextVar;

public:
  Module() : nextVar(1) {}

  std::ostream& print(std::ostream &o) {
    unsigned indent = 0;
    printOpening(o, "module");
    incIndent(o, indent);
    for (auto& curr : functionTypes) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    for (auto& curr : imports) {
      doIndent(o, indent);
      curr.second.print(o, indent);
      o << '\n';
    }
    for (auto& curr : exports) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    doIndent(o, indent);
    table.print(o, indent);
    o << '\n';
    for (auto& curr : functions) {
      doIndent(o, indent);
      curr->print(o, indent);
      o << '\n';
    }
    decIndent(o, indent);
    o << '\n';
  }
};

} // namespace wasm
