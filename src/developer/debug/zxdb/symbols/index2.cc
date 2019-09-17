// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index2.h"

#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/symbols/dwarf_die_decoder.h"
#include "src/developer/debug/zxdb/symbols/dwarf_die_scanner2.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"

namespace zxdb {

namespace {

// Stores a name with a DieRef for later indexing.
class NamedDieRef : public IndexNode2::DieRef {
 public:
  NamedDieRef() = default;

  // The pointed-to string must outlive this class.
  NamedDieRef(bool is_decl, uint32_t offset, IndexNode2::Kind k, const char* name,
              uint32_t decl_offset)
      : DieRef(is_decl, offset), kind_(k), name_(name), decl_offset_(decl_offset) {}

  bool is_valid() const { return kind_ != IndexNode2::Kind::kNone; }

  IndexNode2::Kind kind() const { return kind_; }

  // The name associated with the DIE. Could be null.
  const char* name() const { return name_; }

  // If this DIE has a declaration associated with it (a DW_AT_declaration tag), this indicates the
  // absolute offset of the declaration DIE. Will be 0 if none.
  uint32_t decl_offset() const { return decl_offset_; }

  // The indexing layer uses this to cache the node found for a given thing. This allows us to
  // bypass lookup for the common case of things that are all in the same scope.
  IndexNode2* index_node() const { return index_node_; }
  void set_index_node(IndexNode2* n) { index_node_ = n; }

 private:
  IndexNode2::Kind kind_ = IndexNode2::Kind::kNone;
  const char* name_ = nullptr;
  uint32_t decl_offset_ = 0;
  IndexNode2* index_node_ = nullptr;
};

// Returns true if the given abbreviation defines a PC range.
bool AbbrevHasCode(const llvm::DWARFAbbreviationDeclaration* abbrev) {
  for (const auto spec : abbrev->attributes()) {
    if (spec.Attr == llvm::dwarf::DW_AT_low_pc || spec.Attr == llvm::dwarf::DW_AT_high_pc)
      return true;
  }
  return false;
}

// Returns true if the given abbreviation defines a "location".
bool AbbrevHasLocation(const llvm::DWARFAbbreviationDeclaration* abbrev) {
  for (const auto spec : abbrev->attributes()) {
    if (spec.Attr == llvm::dwarf::DW_AT_location)
      return true;
  }
  return false;
}

// This helper class is used to index the symbols of one unit. It keeps some state to avoid
// reallocating for each call.
//
// Indexing is two passes. In the first pass we scan the DIEs in the unit. We identify which ones
// will need indexing and save information on the nesting. The parent chain information (stored in
// the DwarfDieScanner) is important because we need to go from a DIE to its parent chain, and
// normally walking up the parent chain is a linear search in the LLVM library.
//
// In the second pass we actually index the items identified, using the saved parent and name
// information from the scan pass.
class UnitIndexer {
 public:
  // All passed-in objects must outlive this class.
  explicit UnitIndexer(llvm::DWARFUnit* unit) : unit_(unit), scanner_(unit) {
    // The indexable array is 1:1 with the scanner entries.
    indexable_.resize(scanner_.die_count());

    path_.reserve(8);  // Don't want to reallocate.
  }

  // To use, first call Scan() to populate the indexable_ array, then call Index() to add the
  // items to the given index node root. The Scan pass will additionally add any entrypoint
  // functions it finds to the main_functions vector.
  void Scan(llvm::DWARFContext* context, std::vector<IndexNode2::DieRef>* main_functions);
  void Index(IndexNode2* root);

 private:
  // Returns kNone for non-indexable items.
  IndexNode2::Kind GetKindForDie(const llvm::DWARFDebugInfoEntry* die) const;

  void AddEntryToIndex(uint32_t index_me, IndexNode2* root);

  llvm::DWARFUnit* unit_;

  DwarfDieScanner2 scanner_;
  std::vector<NamedDieRef> indexable_;

  // Variable used for collecting the path of parents in AddDIE. This would make more sense as a
  // local variable but having it here prevents reallocating each time.
  std::vector<uint32_t> path_;
};

// The symbol storage will be filled with the indexable entries.
void UnitIndexer::Scan(llvm::DWARFContext* context,
                       std::vector<IndexNode2::DieRef>* main_functions) {
  DwarfDieDecoder decoder(context, unit_);

  // The offset of the declaration. This can be unit-relative or file-absolute. This code doesn't
  // implement the file-absolute variant which it seems our toolchain doesn't generate. To implement
  // I'm thinking everything with an absolute offset will be put into a global list and processed in
  // a third pass once all units are processed. This third pass will be slower since probably we
  // won't do any optimized lookups.
  llvm::Optional<uint64_t> decl_unit_offset;
  llvm::Optional<uint64_t> decl_global_offset;
  decoder.AddReference(llvm::dwarf::DW_AT_specification, &decl_unit_offset, &decl_global_offset);

  llvm::Optional<bool> is_declaration;
  decoder.AddBool(llvm::dwarf::DW_AT_declaration, &is_declaration);

  llvm::Optional<bool> is_main_subprogram;
  decoder.AddBool(llvm::dwarf::DW_AT_main_subprogram, &is_main_subprogram);

  llvm::Optional<const char*> name;
  decoder.AddCString(llvm::dwarf::DW_AT_name, &name);

  // IF YOU ADD MORE ATTRBUTES HERE don't forget to reset() them before Decode().

  for (; !scanner_.done(); scanner_.Advance()) {
    const llvm::DWARFDebugInfoEntry* die = scanner_.Prepare();

    // Check whether we should consider this before decoding since decoding is slow.
    IndexNode2::Kind kind = GetKindForDie(die);
    if (kind == IndexNode2::Kind::kNone)
      continue;

    // This DIE is of the type we want to index so decode. Must reset all output vars first.
    is_declaration.reset();
    decl_unit_offset.reset();
    decl_global_offset.reset();
    is_main_subprogram.reset();
    name.reset();
    if (!decoder.Decode(*die))
      continue;

    // Compute the offset of a separate declaration if this DIE has one.
    bool decl_offset = 0;
    if (decl_unit_offset)
      decl_offset = unit_->getOffset() + *decl_unit_offset;
    else if (decl_global_offset)
      FXL_NOTREACHED() << "Implement DW_FORM_ref_addr for references.";

    FXL_DCHECK(scanner_.die_index() < indexable_.size());
    indexable_[scanner_.die_index()] = NamedDieRef(
        is_declaration && *is_declaration, die->getOffset(), kind, name ? *name : "", decl_offset);

    // Check for "main" function annotation.
    if (kind == IndexNode2::Kind::kFunction && is_main_subprogram && *is_main_subprogram)
      main_functions->emplace_back(false, die->getOffset());
  }
}

void UnitIndexer::Index(IndexNode2* root) {
  for (uint32_t i = 0; i < indexable_.size(); i++) {
    if (indexable_[i].is_valid())
      AddEntryToIndex(i, root);
  }
}

IndexNode2::Kind UnitIndexer::GetKindForDie(const llvm::DWARFDebugInfoEntry* die) const {
  const llvm::DWARFAbbreviationDeclaration* abbrev = die->getAbbreviationDeclarationPtr();
  if (!abbrev)
    return IndexNode2::Kind::kNone;  // Corrupt.

  switch (static_cast<DwarfTag>(abbrev->getTag())) {
    case DwarfTag::kSubprogram:
      if (AbbrevHasCode(abbrev))
        return IndexNode2::Kind::kFunction;
      return IndexNode2::Kind::kNone;  // Skip functions with no code.

    case DwarfTag::kNamespace:
      return IndexNode2::Kind::kNamespace;

    case DwarfTag::kBaseType:
    case DwarfTag::kClassType:
    case DwarfTag::kEnumerationType:
    case DwarfTag::kPtrToMemberType:
    case DwarfTag::kStringType:
    case DwarfTag::kStructureType:
    case DwarfTag::kSubroutineType:
    case DwarfTag::kUnionType:
      return IndexNode2::Kind::kType;

    case DwarfTag::kVariable:
      if (!scanner_.is_inside_function() && AbbrevHasLocation(abbrev)) {
        // Found variable storage outside of a function (variables inside functions are local so
        // don't get added to the global index).
        // TODO: check function-static variables.
        return IndexNode2::Kind::kVar;
      }
      return IndexNode2::Kind::kNone;  // Variable with no location.

    default:
      // Don't index anything else.
      return IndexNode2::Kind::kNone;
  }
}

void UnitIndexer::AddEntryToIndex(uint32_t index_me, IndexNode2* root) {
  uint32_t cur = index_me;
  if (indexable_[index_me].decl_offset()) {
    // When the entry has a decl_offset, that means it's the implementation for e.g. a function.
    // The actual name comes from the declatation so start from that index.
    llvm::DWARFDie die = unit_->getDIEForOffset(indexable_[index_me].decl_offset());
    if (!die)
      return;  // Invalid declaration.
    cur = unit_->getDIEIndex(die);
  }

  // Don't index more than this number of levels to prevent infinite recursion.
  constexpr uint32_t kMaxPath = 16;

  // Start indexing from here. We may find a cached one that will prevent us from having to
  // go to the root.
  IndexNode2* index_from = root;

  // Collect the path from the current item (path_[0]) to its ultimate parent (path_.back()).
  path_.clear();
  while (cur != DwarfDieScanner2::kNoParent && indexable_[cur].is_valid()) {
    if (path_.size() > kMaxPath)
      return;  // Too many components, consider this item corrupt and don't index.

    if (indexable_[cur].index_node()) {
      // Already indexed this node (for example, this is a namespace that was already referenced).
      // so we can start inserting the path from this node.
      index_from = indexable_[cur].index_node();
      break;
    }
    path_.push_back(cur);
    cur = scanner_.GetParentIndex(cur);
  }

  // Add the path to the index (walk in reverse to start from the root).
  for (int path_i = static_cast<int>(path_.size()) - 1; path_i >= 0; path_i--) {
    NamedDieRef& named_ref = indexable_[path_[path_i]];
    index_from = index_from->AddChild(named_ref.kind(), named_ref.name(), named_ref);
    named_ref.set_index_node(index_from);
  }
}

}  // namespace

void Index2::CreateIndex(llvm::object::ObjectFile* object_file) {
  std::unique_ptr<llvm::DWARFContext> context =
      llvm::DWARFContext::create(*object_file, nullptr, llvm::DWARFContext::defaultErrorHandler);

  llvm::DWARFUnitVector compile_units;
  context->getDWARFObj().forEachInfoSections([&](const llvm::DWARFSection& s) {
    compile_units.addUnitsForSection(*context, s, llvm::DW_SECT_INFO);
  });

  for (unsigned i = 0; i < compile_units.size(); i++) {
    IndexCompileUnit(context.get(), compile_units[i].get(), i);

    // Free compilation units as we process them. They will hold all of the parsed DIE data that we
    // don't need any more which can be multiple GB's for large programs.
    compile_units[i].reset();
  }

  IndexFileNames();
}

void Index2::DumpFileIndex(std::ostream& out) const {
  for (const auto& [filename, file_index_entry] : file_name_index_) {
    const auto& [filepath, compilation_units] = *file_index_entry;
    out << filename << " -> " << filepath << " -> " << compilation_units.size() << " units\n";
  }
}

std::vector<std::string> Index2::FindFileMatches(std::string_view name) const {
  std::string_view name_last_comp = ExtractLastFileComponent(name);

  std::vector<std::string> result;

  // Search all files whose last component matches (the input may contain more than one component).
  FileNameIndex::const_iterator iter = file_name_index_.lower_bound(name_last_comp);
  while (iter != file_name_index_.end() && iter->first == name_last_comp) {
    const auto& pair = *iter->second;
    if (StringEndsWith(pair.first, name) &&
        (pair.first.size() == name.size() ||
         pair.first[pair.first.size() - name.size() - 1] == '/')) {
      result.push_back(pair.first);
    }
    ++iter;
  }

  return result;
}

std::vector<std::string> Index2::FindFilePrefixes(const std::string& prefix) const {
  std::vector<std::string> result;

  auto found = file_name_index_.lower_bound(prefix);
  while (found != file_name_index_.end() && StringBeginsWith(found->first, prefix)) {
    result.push_back(std::string(found->first));
    ++found;
  }
  return result;
}

const std::vector<unsigned>* Index2::FindFileUnitIndices(const std::string& name) const {
  auto found = files_.find(name);
  if (found == files_.end())
    return nullptr;
  return &found->second;
}

void Index2::IndexCompileUnit(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                              unsigned unit_index) {
  UnitIndexer indexer(unit);
  indexer.Scan(context, &main_functions_);
  indexer.Index(&root_);

  IndexCompileUnitSourceFiles(context, unit, unit_index);
}

void Index2::IndexCompileUnitSourceFiles(llvm::DWARFContext* context, llvm::DWARFUnit* unit,
                                         unsigned unit_index) {
  const llvm::DWARFDebugLine::LineTable* line_table = context->getLineTableForUnit(unit);
  if (!line_table)
    return;  // No line table for this unit.

  // This table is the size of the file name table. Entries are set to 1 when we've added them to
  // the index already.
  std::vector<int> added_file;
  added_file.resize(line_table->Prologue.FileNames.size(), 0);

  // We don't want to just add all the files from the line table to the index. The line table will
  // contain entries for every file referenced by the compilation unit, which includes declarations.
  // We want only files that contribute code, which in practice is a tiny fraction of the total.
  //
  // To get this, iterate through the unit's row table and collect all referenced file names.
  std::string file_name;
  for (size_t i = 0; i < line_table->Rows.size(); i++) {
    auto file_id = line_table->Rows[i].File;  // 1-based!
    if (file_id < 1 || file_id > added_file.size())
      continue;
    auto file_index = file_id - 1;

    if (!added_file[file_index]) {
      added_file[file_index] = 1;
      if (line_table->getFileNameByIndex(
              file_id, "", llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
              file_name)) {
        // The files here can contain relative components like "/foo/bar/../baz". This is OK because
        // we want it to match other places in the symbol code that do a similar computation to get
        // a file name.
        files_[file_name].push_back(unit_index);
      }
    }
  }
}

void Index2::IndexFileNames() {
  for (FileIndex::const_iterator iter = files_.begin(); iter != files_.end(); ++iter)
    file_name_index_.insert(std::make_pair(ExtractLastFileComponent(iter->first), iter));
}

}  // namespace zxdb