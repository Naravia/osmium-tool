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

#include "expression.hpp"

#include "../exception.hpp"
#include "../util.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tags_filter_bool {

    namespace {

        constexpr std::size_t max_length = 64U * 1024U;
        constexpr std::size_t max_nodes = 4096;
        constexpr std::size_t max_nesting = 128;

        bool is_space(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

    } // anonymous namespace

    class ExpressionParser {

        enum class TokenKind {
            end,
            predicate,
            conjunction,
            disjunction,
            negation,
            left_parenthesis,
            right_parenthesis
        };

        const std::string& m_source;
        Expression& m_expression;
        std::size_t m_position = 0;
        std::size_t m_token_position = 0;
        TokenKind m_token = TokenKind::end;
        std::string m_text;

        [[noreturn]] void error(std::size_t position, const std::string& message) const {
            throw argument_error{"Boolean expression, column " + std::to_string(position + 1) + ": " + message};
        }

        bool keyword_at(std::size_t position, const char* keyword, std::size_t length) const {
            if (m_source.compare(position, length, keyword) != 0) {
                return false;
            }
            const auto end = position + length;
            return end == m_source.size() || is_space(m_source[end]) || m_source[end] == '(' || m_source[end] == ')';
        }

        bool operator_at(std::size_t position) const {
            return keyword_at(position, "AND", 3) || keyword_at(position, "OR", 2) || keyword_at(position, "NOT", 3);
        }

        void skip_space() noexcept {
            while (m_position < m_source.size() && is_space(m_source[m_position])) {
                ++m_position;
            }
        }

        char escaped_character() {
            const auto position = m_position++;
            if (m_position == m_source.size()) {
                error(position, "unfinished escape");
            }
            return m_source[m_position++];
        }

        void next_token() {
            skip_space();
            m_token_position = m_position;
            m_text.clear();
            if (m_position == m_source.size()) {
                m_token = TokenKind::end;
                return;
            }

            const auto c = m_source[m_position];
            if (c == '(' || c == ')') {
                m_token = c == '(' ? TokenKind::left_parenthesis : TokenKind::right_parenthesis;
                ++m_position;
                return;
            }
            if (keyword_at(m_position, "AND", 3)) {
                m_token = TokenKind::conjunction;
                m_position += 3;
                return;
            }
            if (keyword_at(m_position, "OR", 2)) {
                m_token = TokenKind::disjunction;
                m_position += 2;
                return;
            }
            if (keyword_at(m_position, "NOT", 3)) {
                m_token = TokenKind::negation;
                m_position += 3;
                return;
            }

            m_token = TokenKind::predicate;
            while (m_position < m_source.size()) {
                const auto current = m_source[m_position];
                if (current == '(' || current == ')') {
                    break;
                }
                if (is_space(current)) {
                    auto lookahead = m_position;
                    while (lookahead < m_source.size() && is_space(m_source[lookahead])) {
                        ++lookahead;
                    }
                    if (lookahead == m_source.size() || operator_at(lookahead)) {
                        m_position = lookahead;
                        break;
                    }
                    m_text.append(m_source, m_position, lookahead - m_position);
                    m_position = lookahead;
                    continue;
                }
                if (current == '\\') {
                    m_text += escaped_character();
                } else if (current == '\'' || current == '"') {
                    const auto quote_position = m_position++;
                    while (m_position < m_source.size() && m_source[m_position] != current) {
                        if (m_source[m_position] == '\\') {
                            m_text += escaped_character();
                        } else {
                            m_text += m_source[m_position++];
                        }
                    }
                    if (m_position == m_source.size()) {
                        error(quote_position, "unterminated quote");
                    }
                    ++m_position;
                } else {
                    m_text += m_source[m_position++];
                }
            }
            if (m_text.find_first_not_of(" \t\r\n") == std::string::npos) {
                error(m_token_position, "empty tag predicate");
            }
        }

        std::size_t add_node(Expression::Operation operation, std::size_t matcher, std::vector<std::size_t> children = {}) {
            if (m_expression.m_nodes.size() >= max_nodes) {
                error(m_token_position, "expression exceeds 4096 nodes");
            }
            m_expression.m_nodes.push_back({operation, matcher, std::move(children)});
            return m_expression.m_nodes.size() - 1;
        }

        std::size_t parse_or(std::size_t depth) {
            const auto first = parse_and(depth);
            if (m_token != TokenKind::disjunction) {
                return first;
            }
            std::vector<std::size_t> children{first};
            while (m_token == TokenKind::disjunction) {
                next_token();
                children.push_back(parse_and(depth));
            }
            return add_node(Expression::Operation::any, 0, std::move(children));
        }

        std::size_t parse_and(std::size_t depth) {
            const auto first = parse_unary(depth);
            if (m_token != TokenKind::conjunction) {
                return first;
            }
            std::vector<std::size_t> children{first};
            while (m_token == TokenKind::conjunction) {
                next_token();
                children.push_back(parse_unary(depth));
            }
            return add_node(Expression::Operation::all, 0, std::move(children));
        }

        std::size_t parse_unary(std::size_t depth) {
            if (depth > max_nesting) {
                error(m_token_position, "expression exceeds 128 levels of nesting");
            }
            if (m_token == TokenKind::negation) {
                next_token();
                const auto child = parse_unary(depth + 1);
                return add_node(Expression::Operation::negate, 0, {child});
            }
            if (m_token == TokenKind::left_parenthesis) {
                next_token();
                const auto child = parse_or(depth + 1);
                if (m_token != TokenKind::right_parenthesis) {
                    error(m_token_position, "expected ')'");
                }
                next_token();
                return child;
            }
            if (m_token != TokenKind::predicate) {
                error(m_token_position, "expected tag predicate, NOT, or '('");
            }
            const auto matcher = m_expression.m_matchers.size();
            m_expression.m_matchers.push_back(get_tag_matcher(m_text));
            const auto node = add_node(Expression::Operation::predicate, matcher);
            next_token();
            return node;
        }

    public:

        ExpressionParser(const std::string& source, Expression& expression) :
            m_source(source),
            m_expression(expression) {
        }

        void parse() {
            if (m_source.size() > max_length) {
                error(max_length, "expression exceeds 65536 bytes");
            }
            skip_space();
            // Only a leading, unquoted prefix is a type scope. In particular,
            // a slash after '=' (for example in a URL) is part of the predicate.
            const auto prefix_end = m_source.find_first_of("/=!,*()\"'\\ \t\r\n", m_position);
            if (prefix_end != std::string::npos && m_source[prefix_end] == '/') {
                if (prefix_end != m_position) {
                    try {
                        m_expression.m_entities = get_types(m_source.substr(m_position, prefix_end - m_position));
                    } catch (const argument_error& e) {
                        error(m_position, e.what());
                    }
                }
                m_position = prefix_end + 1;
            }
            next_token();
            m_expression.m_root = parse_or(0);
            if (m_token != TokenKind::end) {
                error(m_token_position, "expected AND, OR, or end of expression");
            }
        }

    }; // class ExpressionParser

    Expression::Expression(const std::string& source) {
        ExpressionParser{source, *this}.parse();
    }

    bool Expression::evaluate(std::size_t index, const osmium::TagList& tags) const noexcept {
        const auto& node = m_nodes[index];
        switch (node.operation) {
            case Operation::predicate:
                return m_matchers[node.matcher](tags);
            case Operation::all:
                for (const auto child : node.children) {
                    if (!evaluate(child, tags)) {
                        return false;
                    }
                }
                return true;
            case Operation::any:
                for (const auto child : node.children) {
                    if (evaluate(child, tags)) {
                        return true;
                    }
                }
                return false;
            case Operation::negate:
                return !evaluate(node.children.front(), tags);
        }
        return false;
    }

    bool Filter::matches(const osmium::TagList& tags) const noexcept {
        return std::any_of(m_expressions.begin(), m_expressions.end(), [&](const Expression& expression) {
            return expression.matches(tags);
        });
    }

    std::string strip_comment(const std::string& line) {
        char quote = 0;
        for (std::size_t pos = 0; pos < line.size(); ++pos) {
            const auto c = line[pos];
            if (c == '\\') {
                if (pos + 1 < line.size()) {
                    ++pos;
                }
            } else if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == '#') {
                return line.substr(0, pos);
            }
        }
        return line;
    }

} // namespace tags_filter_bool
