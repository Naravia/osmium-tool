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

#include <cstddef>
#include <string>
#include <vector>

namespace tags_filter_bool {

    class ExpressionParser;

    // Flat ownership avoids recursive destruction. AND/OR chains have one node
    // with many children, so their length does not increase evaluation depth.
    class Expression {

        friend class ExpressionParser;

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
        std::vector<Node> m_nodes;
        std::size_t m_root = 0;

        bool evaluate(std::size_t index, const osmium::TagList& tags) const noexcept;

    public:

        explicit Expression(const std::string& source);

        osmium::osm_entity_bits::type entities() const noexcept {
            return m_entities;
        }

        bool matches(const osmium::TagList& tags) const noexcept {
            return evaluate(m_root, tags);
        }

    }; // class Expression

    class Filter {

        std::vector<Expression> m_expressions;

    public:

        void add(const Expression& expression) {
            m_expressions.push_back(expression);
        }

        bool empty() const noexcept {
            return m_expressions.empty();
        }

        bool matches(const osmium::TagList& tags) const noexcept;

    }; // class Filter

    // Preserve source offsets for diagnostics while removing file comments.
    std::string strip_comment(const std::string& line);

} // namespace tags_filter_bool

#endif // TAGS_FILTER_BOOL_EXPRESSION_HPP
