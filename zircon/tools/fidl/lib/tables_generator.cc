// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/tables_generator.h"

#include "fidl/names.h"

namespace fidl {

namespace {

std::string PrimitiveSubtypeToString(fidl::types::PrimitiveSubtype subtype) {
  using fidl::types::PrimitiveSubtype;
  switch (subtype) {
    case PrimitiveSubtype::kBool:
      return "Bool";
    case PrimitiveSubtype::kInt8:
      return "Int8";
    case PrimitiveSubtype::kInt16:
      return "Int16";
    case PrimitiveSubtype::kInt32:
      return "Int32";
    case PrimitiveSubtype::kInt64:
      return "Int64";
    case PrimitiveSubtype::kUint8:
      return "Uint8";
    case PrimitiveSubtype::kUint16:
      return "Uint16";
    case PrimitiveSubtype::kUint32:
      return "Uint32";
    case PrimitiveSubtype::kUint64:
      return "Uint64";
    case PrimitiveSubtype::kFloat32:
      return "Float32";
    case PrimitiveSubtype::kFloat64:
      return "Float64";
  }
}

// When generating coding tables for containers employing envelopes (xunions & tables),
// we need to reference coding tables for primitives, in addition to types that need coding.
// This function handles naming coding tables for both cases.
std::string CodedNameForEnvelope(const fidl::coded::Type* type) {
  switch (type->kind) {
    case coded::Type::Kind::kPrimitive: {
      using fidl::types::PrimitiveSubtype;
      // To save space, all primitive types of the same underlying subtype
      // share the same table.
      std::string suffix =
          PrimitiveSubtypeToString(static_cast<const coded::PrimitiveType*>(type)->subtype);
      return "::fidl::internal::k" + suffix;
    }
    default:
      return type->coded_name;
  }
}

constexpr auto kIndent = "    ";

void Emit(std::ostream* file, std::string_view data) { *file << data; }

void EmitNewlineAndIndent(std::ostream* file, size_t indent_level) {
  *file << "\n";
  while (indent_level--)
    *file << kIndent;
}

void EmitArrayBegin(std::ostream* file) { *file << "{"; }

void EmitArraySeparator(std::ostream* file, size_t indent_level) {
  *file << ",";
  EmitNewlineAndIndent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) { *file << "}"; }

void Emit(std::ostream* file, uint32_t value) { *file << value << "u"; }

void Emit(std::ostream* file, uint64_t value) { *file << value << "ul"; }

void Emit(std::ostream* file, types::HandleSubtype handle_subtype) {
  Emit(file, NameHandleZXObjType(handle_subtype));
}

void Emit(std::ostream* file, types::Nullability nullability) {
  switch (nullability) {
    case types::Nullability::kNullable:
      Emit(file, "::fidl::kNullable");
      break;
    case types::Nullability::kNonnullable:
      Emit(file, "::fidl::kNonnullable");
      break;
  }
}

void Emit(std::ostream* file, types::Strictness strictness) {
  switch (strictness) {
    case types::Strictness::kFlexible:
      Emit(file, "::fidl::kFlexible");
      break;
    case types::Strictness::kStrict:
      Emit(file, "::fidl::kStrict");
      break;
  }
}

std::string AltTableReference(const coded::Type& type) {
  return NameTable(std::string(type.coded_name) + "AltTypePointer") + "()";
}

template <typename T>
std::string AltTableDeclaration(const T& type) {
  // We emit "__attribute__((unused))" here instead of [[maybe_unused]], since
  // __attribute__((unused)) is a compiler extension that is widely supported and works with C++14.
  // [[maybe_unused]] is C++17 and above only.
  return "constexpr static inline const " + std::string(GetTypeTraits(type).pointer_name) + " " +
         AltTableReference(type) + " __attribute__((unused));\n";
}

template <typename T>
std::string AltTableDefinition(const T& type, const T& alt_type) {
  assert(type.kind == alt_type.kind);

  auto type_traits = GetTypeTraits(type);

  // We emit both the _declaration_ again, as well as the _definition_, since
  // __attribute__((unused)) can only be applied to a declaration.
  return AltTableDeclaration(type) + "constexpr static inline const " +
         std::string(type_traits.pointer_name) + " " + AltTableReference(type) + " {\n  return &" +
         NameTable(alt_type.coded_name) + "." + std::string(type_traits.fidl_type_accessor) +
         ";\n}\n";
}

// TODO(fxb/7704): Remove templatization when message_type can only be coded struct.
template <typename T>
std::string AltStructFieldDeclaration(const T& message_type, const coded::StructField& field) {
  // We emit "__attribute__((unused))" here instead of [[maybe_unused]], since
  // __attribute__((unused)) is a compiler extension that is widely supported and works with C++14.
  // [[maybe_unused]] is C++17 and above only.
  return "constexpr static inline const ::fidl::FidlStructField* " +
         NameFieldsAltField(message_type.coded_name, field.field_num) + "()" +
         " __attribute__((unused));\n";
}

// TODO(fxb/7704): Remove templatization when message_type can only be coded struct.
template <typename T>
std::string AltStructFieldDefinition(const T& message_type, const coded::StructField& field,
                                     const T& alt_message_type, uint32_t alt_field_index) {
  // We emit both the _declaration_ again, as well as the _definition_, since
  // __attribute__((unused)) can only be applied to a declaration.
  std::ostringstream decl;
  decl << AltStructFieldDeclaration(message_type, field);
  decl << "constexpr static inline const ::fidl::FidlStructField* ";
  decl << NameFieldsAltField(message_type.coded_name, field.field_num) << "()";
  decl << " { return &";
  decl << NameFields(alt_message_type.coded_name) << "[" << alt_field_index << "]";
  decl << "; }\n";
  return decl.str();
}

}  // namespace

void TablesGenerator::GenerateInclude(std::string_view filename) {
  Emit(&tables_file_, "#include ");
  Emit(&tables_file_, filename);
  Emit(&tables_file_, "\n");
}

void TablesGenerator::GenerateFilePreamble() {
  Emit(&tables_file_, "// WARNING: This file is machine generated by fidlc.\n\n");
  GenerateInclude("<lib/fidl/internal.h>");
  Emit(&tables_file_, "\nextern \"C\" {\n");
  Emit(&tables_file_, "\n");
}

void TablesGenerator::GenerateFilePostamble() { Emit(&tables_file_, "} // extern \"C\"\n"); }

template <typename Collection>
void TablesGenerator::GenerateArray(const Collection& collection) {
  EmitArrayBegin(&tables_file_);

  if (!collection.empty())
    EmitNewlineAndIndent(&tables_file_, ++indent_level_);

  for (size_t i = 0; i < collection.size(); ++i) {
    if (i)
      EmitArraySeparator(&tables_file_, indent_level_);
    Generate(collection[i]);
  }

  if (!collection.empty())
    EmitNewlineAndIndent(&tables_file_, --indent_level_);

  EmitArrayEnd(&tables_file_);
}

void TablesGenerator::Generate(const coded::EnumType& enum_type) {
  std::string validator_func = std::string("EnumValidatorFor_") + std::string(enum_type.coded_name);
  Emit(&tables_file_, "static constexpr bool ");
  Emit(&tables_file_, validator_func);
  Emit(&tables_file_, "(uint64_t v) { return ");
  for (const auto& member : enum_type.members) {
    Emit(&tables_file_, "(v == ");
    Emit(&tables_file_, member);
    Emit(&tables_file_, ") || ");
  }
  Emit(&tables_file_, "false; }\n");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(enum_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedEnum(");
  Emit(&tables_file_, "::fidl::FidlCodedPrimitive::k");
  Emit(&tables_file_, PrimitiveSubtypeToString(enum_type.subtype));
  Emit(&tables_file_, ", &" + validator_func + ", \"");
  Emit(&tables_file_, enum_type.qname);
  Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::BitsType& bits_type) {
  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(bits_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedBits(");
  Emit(&tables_file_, "::fidl::FidlCodedPrimitive::k");
  Emit(&tables_file_, PrimitiveSubtypeToString(bits_type.subtype));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, bits_type.mask);
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, bits_type.qname);
  Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::StructType& struct_type) {
  for (const auto field : struct_type.fields) {
    if (field.type)
      Emit(&tables_file_, AltStructFieldDeclaration(struct_type, field));
  }

  Emit(&tables_file_, "static const ::fidl::FidlStructField ");
  Emit(&tables_file_, NameFields(struct_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(struct_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, AltTableDeclaration(struct_type));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(struct_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStruct(");
  Emit(&tables_file_, NameFields(struct_type.coded_name));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, static_cast<uint32_t>(struct_type.fields.size()));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, struct_type.size);
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, struct_type.qname);
  Emit(&tables_file_, "\", ");
  Emit(&tables_file_, AltTableReference(struct_type));
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::TableType& table_type) {
  Emit(&tables_file_, "static const ::fidl::FidlTableField ");
  Emit(&tables_file_, NameFields(table_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(table_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(table_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedTable(");
  Emit(&tables_file_, NameFields(table_type.coded_name));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, static_cast<uint32_t>(table_type.fields.size()));
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, table_type.qname);
  Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::UnionType& union_type) {
  Emit(&tables_file_, "static const ::fidl::FidlUnionField ");
  Emit(&tables_file_, NameFields(union_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(union_type.members);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, AltTableDeclaration(union_type));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(union_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnion(");
  Emit(&tables_file_, NameFields(union_type.coded_name));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, static_cast<uint32_t>(union_type.members.size()));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, union_type.data_offset);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, union_type.size);
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, union_type.qname);
  Emit(&tables_file_, "\", ");
  Emit(&tables_file_, AltTableReference(union_type));
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::XUnionType& xunion_type) {
  Emit(&tables_file_, "static const ::fidl::FidlXUnionField ");
  Emit(&tables_file_, NameFields(xunion_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(xunion_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(xunion_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedXUnion(");
  Emit(&tables_file_, static_cast<uint32_t>(xunion_type.fields.size()));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, NameFields(xunion_type.coded_name));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, xunion_type.nullability);
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, xunion_type.qname);
  Emit(&tables_file_, "\", ");
  Emit(&tables_file_, xunion_type.strictness);
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::PointerType& pointer) {
  switch (pointer.element_type->kind) {
    case coded::Type::Kind::kStruct:
      Emit(&tables_file_, "static const fidl_type_t ");
      Emit(&tables_file_, NameTable(pointer.coded_name));
      Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStructPointer(");
      Generate(pointer.element_type);
      Emit(&tables_file_, ".coded_struct));\n");
      break;
    case coded::Type::Kind::kUnion:
      Emit(&tables_file_, "static const fidl_type_t ");
      Emit(&tables_file_, NameTable(pointer.coded_name));
      Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnionPointer(");
      Generate(pointer.element_type);
      Emit(&tables_file_, ".coded_union));\n");
      break;
    default:
      assert(false && "Invalid pointer element type.");
      break;
  }
}

void TablesGenerator::Generate(const coded::MessageType& message_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(message_type.coded_name));
  Emit(&tables_file_, ";\n");

  for (const auto field : message_type.fields) {
    if (field.type)
      Emit(&tables_file_, AltStructFieldDeclaration(message_type, field));
  }

  Emit(&tables_file_, "static const ::fidl::FidlStructField ");
  Emit(&tables_file_, NameFields(message_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(message_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, AltTableDeclaration(message_type));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(message_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStruct(");
  Emit(&tables_file_, NameFields(message_type.coded_name));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, static_cast<uint32_t>(message_type.fields.size()));
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, message_type.size);
  Emit(&tables_file_, ", \"");
  Emit(&tables_file_, message_type.qname);
  Emit(&tables_file_, "\", ");
  Emit(&tables_file_, AltTableReference(message_type));
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::HandleType& handle_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(handle_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
  Emit(&tables_file_, handle_type.subtype);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, handle_type.nullability);
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::RequestHandleType& request_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(request_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
  Emit(&tables_file_, types::HandleSubtype::kChannel);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, request_type.nullability);
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::ProtocolHandleType& protocol_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(protocol_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
  Emit(&tables_file_, types::HandleSubtype::kChannel);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, protocol_type.nullability);
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::ArrayType& array_type) {
  Emit(&tables_file_, AltTableDeclaration(array_type));

  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(array_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedArray(");
  Generate(array_type.element_type);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, array_type.size);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, array_type.element_size);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, AltTableReference(array_type));
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::StringType& string_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(string_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedString(");
  Emit(&tables_file_, string_type.max_size);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, string_type.nullability);
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::VectorType& vector_type) {
  Emit(&tables_file_, AltTableDeclaration(vector_type));

  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(vector_type.coded_name));
  Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedVector(");
  Generate(vector_type.element_type);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, vector_type.max_count);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, vector_type.element_size);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, vector_type.nullability);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, AltTableReference(vector_type));
  Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::Type* type) {
  if (type && type->coding_needed == coded::CodingNeeded::kAlways) {
    Emit(&tables_file_, "&");
    Emit(&tables_file_, NameTable(CodedNameForEnvelope(type)));
  } else {
    Emit(&tables_file_, "nullptr");
  }
}

void TablesGenerator::Generate(const coded::StructField& field) {
  Emit(&tables_file_, "::fidl::FidlStructField(");
  Generate(field.type);
  Emit(&tables_file_, ", ");
  if (field.type) {
    Emit(&tables_file_, field.offset);
  } else {
    Emit(&tables_file_, field.offset + field.size);
  }
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, field.padding);
  if (field.type) {
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, NameFieldsAltField(field.struct_type->coded_name, field.field_num) + "()");
  }
  Emit(&tables_file_, ")");
}

void TablesGenerator::Generate(const coded::UnionField& field) {
  Emit(&tables_file_, "::fidl::FidlUnionField(");
  Generate(field.type);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, field.padding);
  Emit(&tables_file_, ", ");
  Emit(&tables_file_, field.xunion_ordinal);
  Emit(&tables_file_, ")");
}

void TablesGenerator::Generate(const coded::TableField& field) {
  Emit(&tables_file_, "::fidl::FidlTableField(");
  Generate(field.type);
  Emit(&tables_file_, ",");
  Emit(&tables_file_, field.ordinal);
  Emit(&tables_file_, ")");
}

void TablesGenerator::Generate(const coded::XUnionField& field) {
  Emit(&tables_file_, "::fidl::FidlXUnionField(");
  Generate(field.type);
  Emit(&tables_file_, ",");
  Emit(&tables_file_, field.ordinal);
  Emit(&tables_file_, ")");
}

void TablesGenerator::GenerateForward(const coded::EnumType& enum_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(enum_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::BitsType& bits_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(bits_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::StructType& struct_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(struct_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::TableType& table_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(table_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::UnionType& union_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(union_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::XUnionType& xunion_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(xunion_type.coded_name));
  Emit(&tables_file_, ";\n");
}

void TablesGenerator::Produce(CodedTypesGenerator* coded_types_generator,
                              const WireFormat wire_format) {
  coded_types_generator->CompileCodedTypes(wire_format);

  // Generate forward declarations of coding tables for named declarations.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
        GenerateForward(*static_cast<const coded::EnumType*>(coded_type));
        break;
      case coded::Type::Kind::kBits:
        GenerateForward(*static_cast<const coded::BitsType*>(coded_type));
        break;
      case coded::Type::Kind::kStruct:
        GenerateForward(*static_cast<const coded::StructType*>(coded_type));
        break;
      case coded::Type::Kind::kTable:
        GenerateForward(*static_cast<const coded::TableType*>(coded_type));
        break;
      case coded::Type::Kind::kUnion:
        GenerateForward(*static_cast<const coded::UnionType*>(coded_type));
        break;
      case coded::Type::Kind::kXUnion: {
        // Generate forward declarations for both the non-nullable and nullable variants
        const auto& xunion_type = *static_cast<const coded::XUnionType*>(coded_type);
        GenerateForward(xunion_type);
        assert(xunion_type.maybe_reference_type != nullptr &&
               "Named coded xunion must have a reference type!");
        GenerateForward(*xunion_type.maybe_reference_type);
        break;
      }
      default:
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate pointer coding tables necessary for nullable types.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kStruct: {
        const auto& struct_type = *static_cast<const coded::StructType*>(coded_type);
        if (auto pointer_type = struct_type.maybe_reference_type; pointer_type) {
          Generate(*pointer_type);
        }
        break;
      }
      case coded::Type::Kind::kUnion: {
        const auto& union_type = *static_cast<const coded::UnionType*>(coded_type);
        if (auto pointer_type = union_type.maybe_reference_type; pointer_type) {
          Generate(*pointer_type);
        }
        break;
      }
      case coded::Type::Kind::kXUnion: {
        // Nullable xunions have the same wire representation as non-nullable ones,
        // hence have the same fields and dependencies in their coding tables.
        // As such, we will generate them in the next phase, to maintain the correct
        // declaration order.
        break;
      }
      default:
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate coding table definitions for unnamed declarations.
  // These are composed in an ad-hoc way in FIDL source, hence we generate "static" coding tables
  // local to the translation unit.
  for (const auto& coded_type : coded_types_generator->coded_types()) {
    if (coded_type->coding_needed == coded::CodingNeeded::kEnvelopeOnly)
      continue;

    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
      case coded::Type::Kind::kBits:
      case coded::Type::Kind::kStruct:
      case coded::Type::Kind::kTable:
      case coded::Type::Kind::kUnion:
      case coded::Type::Kind::kPointer:
      case coded::Type::Kind::kXUnion:
        // These are generated in the next phase.
        break;
      case coded::Type::Kind::kProtocol:
        // Nothing to generate for protocols. We've already moved the
        // messages from the protocol into coded_types_ directly.
        break;
      case coded::Type::Kind::kMessage:
        Generate(*static_cast<const coded::MessageType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kHandle:
        Generate(*static_cast<const coded::HandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kProtocolHandle:
        Generate(*static_cast<const coded::ProtocolHandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kRequestHandle:
        Generate(*static_cast<const coded::RequestHandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kArray:
        Generate(*static_cast<const coded::ArrayType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kString:
        Generate(*static_cast<const coded::StringType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kVector:
        Generate(*static_cast<const coded::VectorType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kPrimitive:
        // Nothing to generate for primitives. We intern all primitive
        // coding tables, and therefore directly reference them.
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate coding table definitions for named declarations.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    // Definition will be generated elsewhere.
    if (decl->name.library() != coded_types_generator->library())
      continue;

    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
        Generate(*static_cast<const coded::EnumType*>(coded_type));
        break;
      case coded::Type::Kind::kBits:
        Generate(*static_cast<const coded::BitsType*>(coded_type));
        break;
      case coded::Type::Kind::kStruct:
        Generate(*static_cast<const coded::StructType*>(coded_type));
        break;
      case coded::Type::Kind::kTable:
        Generate(*static_cast<const coded::TableType*>(coded_type));
        break;
      case coded::Type::Kind::kUnion:
        Generate(*static_cast<const coded::UnionType*>(coded_type));
        break;
      case coded::Type::Kind::kXUnion: {
        const auto& xunion_type = *static_cast<const coded::XUnionType*>(coded_type);
        Generate(xunion_type);
        assert(xunion_type.maybe_reference_type != nullptr &&
               "Named coded xunion must have a reference type!");
        Generate(*xunion_type.maybe_reference_type);
        break;
      }
      default:
        break;
    }
  }
}

std::ostringstream TablesGenerator::Produce() {
  GenerateFilePreamble();

  Emit(&tables_file_, "// Coding tables for old wire format.\n\n");
  CodedTypesGenerator ctg_old(library_);
  Produce(&ctg_old, WireFormat::kOld);

  Emit(&tables_file_, "// Coding tables for v1 wire format.\n\n");
  CodedTypesGenerator ctg_v1(library_);
  Produce(&ctg_v1, WireFormat::kV1NoEe);

  Emit(&tables_file_, "// Old <-> V1 map.\n\n");

  auto old_coded_types = ctg_old.AllCodedTypes();
  auto v1_coded_types = ctg_v1.AllCodedTypes();
  assert(old_coded_types.size() == v1_coded_types.size());

  for (size_t i = 0; i < old_coded_types.size(); i++) {
    const auto old = old_coded_types[i];
    const auto v1 = v1_coded_types[i];

    assert((old && v1) || (!old && !v1));
    assert(old->kind == v1->kind);

    switch (old->kind) {
      using Kind = coded::Type::Kind;

      case Kind::kUnion:
      case Kind::kArray:
      case Kind::kVector:
        Emit(&tables_file_, AltTableDefinition(*old, *v1));
        Emit(&tables_file_, "\n");
        Emit(&tables_file_, AltTableDefinition(*v1, *old));
        Emit(&tables_file_, "\n\n");
        break;
      case Kind::kStruct: {
        Emit(&tables_file_, AltTableDefinition(*old, *v1));
        Emit(&tables_file_, "\n");
        Emit(&tables_file_, AltTableDefinition(*v1, *old));
        Emit(&tables_file_, "\n\n");
        auto old_struct_type = static_cast<const coded::StructType&>(*old);
        auto v1_struct_type = static_cast<const coded::StructType&>(*v1);
        ProduceStructFieldLinking(old_struct_type, v1_struct_type);
        break;
      }
      case Kind::kMessage: {
        Emit(&tables_file_, AltTableDefinition(*old, *v1));
        Emit(&tables_file_, "\n");
        Emit(&tables_file_, AltTableDefinition(*v1, *old));
        Emit(&tables_file_, "\n\n");
        auto old_message_type = static_cast<const coded::MessageType&>(*old);
        auto v1_message_type = static_cast<const coded::MessageType&>(*v1);
        ProduceStructFieldLinking(old_message_type, v1_message_type);
        break;
      }
      case Kind::kPrimitive:
      case Kind::kEnum:
      case Kind::kBits:
      case Kind::kHandle:
      case Kind::kProtocolHandle:
      case Kind::kRequestHandle:
      case Kind::kTable:
      case Kind::kXUnion:
      case Kind::kPointer:
      case Kind::kProtocol:
      case Kind::kString:
        break;
    }
  }

  GenerateFilePostamble();

  return std::move(tables_file_);
}

namespace {

std::map<uint32_t, uint32_t> MapFieldNumToAltFieldIndexInCodedStruct(
      const std::vector<coded::StructField>& fields,
      const std::vector<coded::StructField>& alt_fields) {
  std::map<uint32_t, uint32_t> mapping;
  for (auto field : fields) {
    uint32_t alt_field_index_in_coded_struct = 0;
    for (auto alt_field : alt_fields) {
      if (field.field_num == alt_field.field_num) {
        mapping.emplace(field.field_num, alt_field_index_in_coded_struct);
        break;
      }
      alt_field_index_in_coded_struct++;
    }
  }
  return mapping;
}

}

// TODO(fxb/7704): Remove templatization when message_type can only be coded struct.
template <typename T>
void TablesGenerator::ProduceStructFieldLinking(const T& old, const T& v1) {
  auto ProductLinksForFields = [&](const T& struct_type, const T& alt_struct_type) {
    auto field_num_to_alt_field_index_in_coded_struct =
        MapFieldNumToAltFieldIndexInCodedStruct(struct_type.fields, alt_struct_type.fields);
    for (const auto field : struct_type.fields) {
      if (!field.type) {
        continue;
      }
      uint32_t alt_field_index =
          field_num_to_alt_field_index_in_coded_struct.find(field.field_num)->second;
      Emit(&tables_file_, AltStructFieldDefinition(struct_type, field,
                                                   alt_struct_type, alt_field_index));
    }
  };
  ProductLinksForFields(old, v1);
  ProductLinksForFields(v1, old);
}

}  // namespace fidl
