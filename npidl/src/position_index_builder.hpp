// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "position_index.hpp"
#include "ast.hpp"

namespace npidl {

// Builds a PositionIndex from a parsed Context
class PositionIndexBuilder {
    PositionIndex& index_;
    Context& ctx_;
    
public:
    PositionIndexBuilder(PositionIndex& index, Context& ctx) 
        : index_(index), ctx_(ctx) {}
    
    // Build the complete index from the context
    void build() {
        // Index imports
        for (auto* import : ctx_.imports) {
            if (import->import_line > 0) {  // Has position info
                index_.add(
                    import,
                    PositionIndex::NodeType::Import,
                    import->import_line,
                    import->import_col,
                    import->import_line,
                    import->path_end_col
                );
            }
        }
        
        // Index interfaces
        for (auto* interface : ctx_.interfaces) {
            index_interface(interface);
        }
        
        // Index exceptions (they're stored separately but are actually structs)
        for (auto* exception : ctx_.exceptions) {
            index_struct(exception, true);
        }
        
        // Finalize (sort) the index
        index_.finalize();
    }
    
private:
    void index_interface(AstInterfaceDecl* ifs) {
        if (!has_position(ifs)) return;
        
        index_.add(
            ifs,
            PositionIndex::NodeType::Interface,
            ifs->range.start.line,
            ifs->range.start.column,
            ifs->range.end.line,
            ifs->range.end.column
        );
        
        // Index functions within interface
        for (auto* fn : ifs->fns) {
            index_function(fn);
        }
    }
    
    void index_struct(AstStructDecl* s, bool is_exception = false) {
        if (!has_position(s)) return;
        
        index_.add(
            s,
            is_exception ? PositionIndex::NodeType::Exception : PositionIndex::NodeType::Struct,
            s->range.start.line,
            s->range.start.column,
            s->range.end.line,
            s->range.end.column
        );
        
        // Index fields within struct
        for (auto* field : s->fields) {
            index_field(field);
        }
    }
    
    void index_function(AstFunctionDecl* fn) {
        if (!has_position(fn)) return;
        
        index_.add(
            fn,
            PositionIndex::NodeType::Function,
            fn->range.start.line,
            fn->range.start.column,
            fn->range.end.line,
            fn->range.end.column
        );
        
        // Index parameters
        for (auto* arg : fn->args) {
            if (has_position(arg)) {
                index_.add(
                    arg,
                    PositionIndex::NodeType::Parameter,
                    arg->range.start.line,
                    arg->range.start.column,
                    arg->range.end.line,
                    arg->range.end.column
                );
            }
        }
    }
    
    void index_field(AstFieldDecl* field) {
        if (!has_position(field)) return;
        
        index_.add(
            field,
            PositionIndex::NodeType::Field,
            field->range.start.line,
            field->range.start.column,
            field->range.end.line,
            field->range.end.column
        );
    }
    
    // Helper to check if a node has position information
    template<typename T>
    bool has_position(T* node) {
        return node && node->range.start.line > 0;
    }
};

} // namespace npidl
