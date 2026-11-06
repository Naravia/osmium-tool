#ifndef TAGS_FILTER_BOOL_EXPRESSION_HPP
#define TAGS_FILTER_BOOL_EXPRESSION_HPP

/*

Osmium -- OpenStreetMap data manipulation command line tool
https://osmcode.org/osmium-tool/

Copyright (C) 2013-2026  Jochen Topf <jochen@topf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include <osmium/osm/entity_bits.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/tags/matcher.hpp>
#include <osmium/tags/tags_filter.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tags_filter_bool {

    class ExpressionParser;

    // Flat ownership avoids recursive destruction. AND/OR chains have one node
    // with many children, so their length does not increase evaluation depth.
    class Expression {

        friend class ExpressionParser;
        friend class Filter;

        enum class Operation {
            predicate,
            all,
            any,
            negate
        };

        struct Node {
            Operation operation;
            std::size_t matcher;
            std::vector<std::size_t> children;
        };

        osmium::osm_entity_bits::type m_entities = osmium::osm_entity_bits::nwr;
        std::vector<osmium::TagMatcher> m_matchers;
        std::vector<std::string> m_matcher_sources;
        std::vector<Node> m_nodes;
        std::size_t m_root = 0;
        bool m_matches_empty = false;
        bool m_is_atomic_or = false;

        bool evaluate(std::size_t index, const osmium::TagList& tags) const noexcept;
        bool evaluate_empty(std::size_t index) const noexcept;

    public:

        explicit Expression(const std::string& source);

        osmium::osm_entity_bits::type entities() const noexcept {
            return m_entities;
        }

        bool matches(const osmium::TagList& tags) const noexcept {
            return tags.empty() ? m_matches_empty : evaluate(m_root, tags);
        }

    }; // class Expression

    class Filter {

    public:

        // Scratch belongs to the caller, not the immutable matching plan.
        // Construct after adding expressions; matching never grows this buffer.
        class Context {

            friend class Filter;

            struct Entry {
                std::uint64_t generation = 0;
                bool value = false;
            };

            std::vector<Entry> m_entries;
            std::uint64_t m_generation = 0;

            explicit Context(std::size_t size) :
                m_entries(size) {
            }

            void begin_match() noexcept;

        public:

            Context() = default;

        }; // class Context

    private:

        struct Program {
            std::vector<Expression::Node> nodes;
            std::size_t root;
        };

        std::vector<Program> m_expressions;
        std::vector<osmium::TagMatcher> m_predicates;
        std::unordered_map<std::string, std::size_t> m_predicate_ids;
        osmium::TagsFilter m_atomic_filter;
        bool m_matches_empty = false;
        bool m_use_atomic_or = true;

        template <bool Cached>
        bool evaluate(const Program& expression, std::size_t index, const osmium::TagList& tags, Context* context) const noexcept;

    public:

        void add(const Expression& expression);

        bool empty() const noexcept {
            return m_expressions.empty();
        }

        std::size_t predicate_count() const noexcept {
            return m_predicates.size();
        }

        Context make_context() const {
            return Context{m_predicates.size()};
        }

        // The context-free overload remains allocation-free and uncached.
        bool matches(const osmium::TagList& tags) const noexcept;
        bool matches(const osmium::TagList& tags, Context& context) const noexcept;

    }; // class Filter

    // Preserve source offsets for diagnostics while removing file comments.
    std::string strip_comment(const std::string& line);

} // namespace tags_filter_bool

#endif // TAGS_FILTER_BOOL_EXPRESSION_HPP
