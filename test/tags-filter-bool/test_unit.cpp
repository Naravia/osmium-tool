
#include "test.hpp" // IWYU pragma: keep

#include "tags_filter_bool/expression.hpp"

#include <osmium/builder/attr.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/entity_bits.hpp>
#include <osmium/osm/node.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

bool matches(const std::string& source, const osmium::builder::attr::tag_ilist& tags = {}) {
    osmium::memory::Buffer buffer{1024, osmium::memory::Buffer::auto_grow::yes};
    const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tags(tags));
    return tags_filter_bool::Expression{source}.matches(buffer.get<osmium::Node>(offset).tags());
}

struct BooleanFormula {
    std::string source;
    std::uint32_t truth_table;
};

BooleanFormula generate_formula(std::mt19937& random, unsigned int depth) {
    const auto operation = depth == 0 ? 0 : random() % 4;
    if (operation == 0) {
        const auto variable = random() % 5;
        std::uint32_t truth_table = 0;
        for (unsigned int assignment = 0; assignment < 32; ++assignment) {
            if ((assignment & (1U << variable)) != 0) {
                truth_table |= std::uint32_t{1} << assignment;
            }
        }
        return {std::string(1, static_cast<char>('a' + variable)), truth_table};
    }
    const auto left = generate_formula(random, depth - 1);
    if (operation == 1) {
        return {"NOT (" + left.source + ")", ~left.truth_table};
    }
    const auto right = generate_formula(random, depth - 1);
    if (operation == 2) {
        return {"(" + left.source + " AND " + right.source + ")", left.truth_table & right.truth_table};
    }
    return {"(" + left.source + " OR " + right.source + ")", left.truth_table | right.truth_table};
}

} // anonymous namespace

TEST_CASE("Boolean tag predicates match across distinct tags", "[tags-filter-bool]") {
    const std::string source{"key1=value1 AND key2=value2 AND (key3!= OR key4!=)"};
    REQUIRE(matches(source, {{"key1", "value1"}, {"key2", "value2"}, {"key3", "yes"}}));
    REQUIRE(matches(source, {{"key1", "value1"}, {"key2", "value2"}, {"key3", ""}, {"key4", "yes"}}));
    REQUIRE_FALSE(matches(source, {{"key1", "value1"}, {"key2", "value2"}, {"key3", ""}}));
    REQUIRE_FALSE(matches(source, {{"key1", "value1"}, {"key3", "yes"}}));
    REQUIRE_FALSE(matches(source, {{"key1", "wrong"}, {"key2", "value2"}, {"key4", "yes"}}));
}

TEST_CASE("Boolean operators obey precedence and grouping", "[tags-filter-bool]") {
    for (unsigned int bits = 0; bits < 8; ++bits) {
        std::vector<std::pair<std::string, std::string>> tags;
        for (unsigned int i = 0; i < 3; ++i) {
            if ((bits & (1U << i)) != 0) {
                tags.emplace_back(std::string(1, static_cast<char>('a' + i)), "yes");
            }
        }
        osmium::memory::Buffer buffer{1024};
        const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tags(tags));
        const auto& tag_list = buffer.get<osmium::Node>(offset).tags();
        const bool a = (bits & 1U) != 0;
        const bool b = (bits & 2U) != 0;
        const bool c = (bits & 4U) != 0;
        REQUIRE(tags_filter_bool::Expression{"a OR b AND c"}.matches(tag_list) == (a || (b && c)));
        REQUIRE(tags_filter_bool::Expression{"(a OR b) AND c"}.matches(tag_list) == ((a || b) && c));
        REQUIRE(tags_filter_bool::Expression{"NOT a AND b OR c"}.matches(tag_list) == ((!a && b) || c));
        REQUIRE(tags_filter_bool::Expression{"NOT (a OR NOT (b AND c))"}.matches(tag_list) == !(a || !(b && c)));
        REQUIRE(tags_filter_bool::Expression{"NOT NOT a"}.matches(tag_list) == a);
    }
}

TEST_CASE("Boolean missing and empty value semantics", "[tags-filter-bool]") {
    REQUIRE_FALSE(matches("key"));
    REQUIRE_FALSE(matches("key="));
    REQUIRE_FALSE(matches("key!="));
    REQUIRE_FALSE(matches("key!=value"));
    REQUIRE(matches("NOT key"));
    REQUIRE(matches("NOT key=value"));

    REQUIRE(matches("key", {{"key", ""}}));
    REQUIRE(matches("key=", {{"key", ""}}));
    REQUIRE_FALSE(matches("key!=", {{"key", ""}}));
    REQUIRE_FALSE(matches("NOT key", {{"key", ""}}));

    REQUIRE(matches("key", {{"key", "value"}}));
    REQUIRE_FALSE(matches("key=", {{"key", "value"}}));
    REQUIRE(matches("key!=", {{"key", "value"}}));
    REQUIRE(matches("key!=", {{"key", " "}}));
    REQUIRE_FALSE(matches("NOT key", {{"key", "value"}}));

    // Each leaf retains existential TagMatcher semantics, even for duplicate keys.
    REQUIRE(matches("key=one AND key=two", {{"key", "one"}, {"key", "two"}}));
    REQUIRE_FALSE(matches("NOT key=one", {{"key", "one"}, {"key", "two"}}));
    REQUIRE(matches("key!=one", {{"key", "one"}, {"key", "two"}}));
}

TEST_CASE("Generated Boolean expressions agree with independent truth tables", "[tags-filter-bool]") {
    std::vector<osmium::memory::Buffer> buffers;
    for (unsigned int assignment = 0; assignment < 32; ++assignment) {
        std::vector<std::pair<std::string, std::string>> tags;
        for (unsigned int variable = 0; variable < 5; ++variable) {
            if ((assignment & (1U << variable)) != 0) {
                tags.emplace_back(std::string(1, static_cast<char>('a' + variable)), "yes");
            }
        }
        buffers.emplace_back(1024);
        osmium::builder::add_node(buffers.back(), osmium::builder::attr::_tags(tags));
    }
    std::mt19937 random{12345};
    for (unsigned int i = 0; i < 100; ++i) {
        const auto formula = generate_formula(random, 6);
        const tags_filter_bool::Expression expression{formula.source};
        tags_filter_bool::Filter filter;
        filter.add(expression);
        auto context = filter.make_context();
        for (unsigned int assignment = 0; assignment < 32; ++assignment) {
            INFO(formula.source);
            const bool expected = (formula.truth_table & (std::uint32_t{1} << assignment)) != 0;
            REQUIRE(expression.matches(buffers[assignment].get<osmium::Node>(0).tags()) == expected);
            REQUIRE(filter.matches(buffers[assignment].get<osmium::Node>(0).tags()) == expected);
            REQUIRE(filter.matches(buffers[assignment].get<osmium::Node>(0).tags(), context) == expected);
        }
    }
}

TEST_CASE("Boolean leaves retain existing matcher syntax", "[tags-filter-bool]") {
    REQUIRE(matches("highway=primary,secondary AND name,name:de!=", {{"highway", "secondary"}, {"name:de", "Berlin"}}));
    REQUIRE(matches("addr:* AND name=*Paris", {{"addr:city", "Paris"}, {"name", "East Paris"}}));
    REQUIRE(matches("name=New York AND building", {{"name", "New York"}, {"building", "yes"}}));
    REQUIRE(matches(" key = value \t AND\n other ", {{"key", "value"}, {"other", ""}}));
    REQUIRE(matches("*", {{"anything", ""}}));
    REQUIRE_FALSE(matches("*"));
    REQUIRE_FALSE(matches("Key=value", {{"key", "value"}}));
    REQUIRE(matches("name=AND", {{"name", "AND"}}));
    REQUIRE(matches("name=and or not", {{"name", "and or not"}}));
}

TEST_CASE("Boolean quoting and escapes protect tokenizer delimiters", "[tags-filter-bool]") {
    REQUIRE(matches("name=\"Rock AND Roll\" AND building", {{"name", "Rock AND Roll"}, {"building", "yes"}}));
    REQUIRE(matches("name='Cafe (West)'", {{"name", "Cafe (West)"}}));
    REQUIRE(matches("\"AND\"", {{"AND", "yes"}}));
    REQUIRE(matches("\\AND", {{"AND", "yes"}}));
    REQUIRE(matches("name=Cafe\\(West\\)", {{"name", "Cafe(West)"}}));
    REQUIRE(matches("name=\"A \\\"B\\\"\"", {{"name", "A \"B\""}}));
    REQUIRE(matches("name='O\\'Brien'", {{"name", "O'Brien"}}));
    REQUIRE(matches("name=C:\\\\data", {{"name", "C:\\data"}}));
    REQUIRE(matches("name=\"a,b\"", {{"name", "a"}}));
    REQUIRE(matches("name=\"x*\"", {{"name", "xyz"}}));
    REQUIRE(matches("\"a/b\"", {{"a/b", "yes"}}));
    REQUIRE(matches("website=https://example.com/a AND name", {{"website", "https://example.com/a"}, {"name", "yes"}}));
    REQUIRE(matches("name=#literal", {{"name", "#literal"}}));
}

TEST_CASE("Boolean scopes apply to complete expressions", "[tags-filter-bool]") {
    using osmium::osm_entity_bits::nwr;
    REQUIRE(tags_filter_bool::Expression{"NOT name"}.entities() == nwr);
    REQUIRE(tags_filter_bool::Expression{"/NOT name"}.entities() == nwr);
    REQUIRE(tags_filter_bool::Expression{" nw/(a OR b) "}.entities() == (osmium::osm_entity_bits::node | osmium::osm_entity_bits::way));
    REQUIRE(tags_filter_bool::Expression{"a/NOT name"}.entities() == osmium::osm_entity_bits::area);
    REQUIRE(tags_filter_bool::Expression{"r/type=multipolygon"}.entities() == osmium::osm_entity_bits::relation);
    REQUIRE(tags_filter_bool::Expression{"website=https://example.com"}.entities() == nwr);
    REQUIRE(tags_filter_bool::Expression{"'a/b'"}.entities() == nwr);
    REQUIRE_THROWS_AS(tags_filter_bool::Expression{"x/name"}, argument_error);
}

TEST_CASE("Boolean syntax errors include source columns", "[tags-filter-bool]") {
    const std::vector<std::string> invalid{
        "", " \t\r", "n/", "()", "a AND", "OR a", "NOT", "a OR OR b",
        "(a", "a)", "a (b)", "a NOT b", "a AND ()", "name='unclosed", "name=unfinished\\", "''"
    };
    for (const auto& source : invalid) {
        INFO(source);
        try {
            const tags_filter_bool::Expression expression{source};
            FAIL("Expected a syntax error");
        } catch (const argument_error& e) {
            REQUIRE(std::string{e.what()}.find("Boolean expression, column ") == 0);
        }
    }
    try {
        const tags_filter_bool::Expression expression{"a AND"};
        FAIL("Expected a syntax error");
    } catch (const argument_error& e) {
        REQUIRE(std::string{e.what()}.find("column 6:") != std::string::npos);
    }
}

TEST_CASE("Boolean expression complexity is bounded", "[tags-filter-bool]") {
    REQUIRE_THROWS_AS(tags_filter_bool::Expression{std::string(65537, 'a')}, argument_error);
    REQUIRE(matches(std::string(65536, 'a')) == false);
    REQUIRE_THROWS_AS(tags_filter_bool::Expression{std::string(129, '(') + "a" + std::string(129, ')')}, argument_error);
    REQUIRE(matches(std::string(128, '(') + "a" + std::string(128, ')'), {{"a", "yes"}}));
    std::string negations;
    for (std::size_t i = 0; i < 129; ++i) {
        negations += "NOT ";
    }
    REQUIRE_THROWS_AS(tags_filter_bool::Expression{negations + "a"}, argument_error);

    std::string chain{"a"};
    for (std::size_t i = 1; i < 4095; ++i) {
        chain += " OR a";
    }
    REQUIRE(matches(chain, {{"a", "yes"}}));
    REQUIRE_FALSE(matches(chain));
    chain += " OR a";
    REQUIRE_THROWS_AS(tags_filter_bool::Expression{chain}, argument_error);

    std::string conjunction{"a"};
    for (std::size_t i = 1; i < 4095; ++i) {
        conjunction += " AND a";
    }
    REQUIRE(matches(conjunction, {{"a", "yes"}}));
}

TEST_CASE("Boolean expression-file comments respect quotes and escapes", "[tags-filter-bool]") {
    REQUIRE(tags_filter_bool::strip_comment("a AND b # comment") == "a AND b ");
    REQUIRE(tags_filter_bool::strip_comment("name='#one' # comment") == "name='#one' ");
    REQUIRE(tags_filter_bool::strip_comment("name=\"#one\" # comment") == "name=\"#one\" ");
    REQUIRE(tags_filter_bool::strip_comment("name=\\#one # comment") == "name=\\#one ");
    REQUIRE(tags_filter_bool::strip_comment("name=\"escaped\\\"#one\" # comment") == "name=\"escaped\\\"#one\" ");
    REQUIRE(tags_filter_bool::strip_comment("name=\\\\#comment") == "name=\\\\");
    REQUIRE(tags_filter_bool::strip_comment("# comment").empty());
}

TEST_CASE("Boolean filter alternatives are ORed", "[tags-filter-bool]") {
    osmium::memory::Buffer buffer{1024};
    const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tag("b", "yes"));
    const auto& tags = buffer.get<osmium::Node>(offset).tags();
    tags_filter_bool::Filter filter;
    REQUIRE(filter.empty());
    REQUIRE_FALSE(filter.matches(tags));
    filter.add(tags_filter_bool::Expression{"a"});
    REQUIRE_FALSE(filter.empty());
    REQUIRE_FALSE(filter.matches(tags));
    filter.add(tags_filter_bool::Expression{"b AND NOT a"});
    REQUIRE(filter.matches(tags));
}

TEST_CASE("Boolean filters cache empty-tag results without losing negation semantics", "[tags-filter-bool]") {
    osmium::memory::Buffer buffer{1024};
    const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_id(1));
    const auto& tags = buffer.get<osmium::Node>(offset).tags();
    const std::vector<std::pair<std::string, bool>> cases{
        {"key", false}, {"key=", false}, {"key!=", false}, {"key!=value", false},
        {"*", false}, {"key OR other", false}, {"NOT key", true},
        {"NOT key=value", true}, {"NOT NOT key", false},
        {"NOT (key OR other)", true}, {"NOT key AND NOT other", true},
        {"NOT key AND other", false}, {"NOT (key AND NOT other)", true}
    };
    for (const auto& entry : cases) {
        INFO(entry.first);
        const tags_filter_bool::Expression expression{entry.first};
        tags_filter_bool::Filter filter;
        REQUIRE_FALSE(filter.matches(tags));
        filter.add(expression);
        REQUIRE(expression.matches(tags) == entry.second);
        REQUIRE(filter.matches(tags) == entry.second);
        filter.add(tags_filter_bool::Expression{"missing"});
        REQUIRE(filter.matches(tags) == entry.second);
        filter.add(tags_filter_bool::Expression{"NOT missing"});
        REQUIRE(filter.matches(tags));
    }
}

TEST_CASE("Atomic-OR filters agree with expression evaluation", "[tags-filter-bool]") {
    const std::vector<std::string> sources{
        "key", "key!=", "key!=one", "key=", "*", "key=one OR key=two",
        "key=one OR (other OR key=two)", "key=one,three OR name,name:de!=",
        "addr:* OR name=*Paris", "name='Rock AND Roll' OR key=two"
    };
    const std::vector<std::vector<std::pair<std::string, std::string>>> tag_sets{
        {}, {{"key", ""}}, {{"key", "one"}}, {{"key", "two"}},
        {{"key", "one"}, {"key", "two"}}, {{"unrelated", "yes"}},
        {{"name:de", "Berlin"}}, {{"addr:city", "Paris"}},
        {{"name", "East Paris"}}, {{"name", "Rock AND Roll"}}
    };
    for (const auto& source : sources) {
        INFO(source);
        const tags_filter_bool::Expression expression{source};
        tags_filter_bool::Filter filter;
        filter.add(expression);
        // Repeated rules must not change the existential result.
        filter.add(expression);
        const auto copied = filter;
        for (const auto& tag_set : tag_sets) {
            osmium::memory::Buffer buffer{1024};
            const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tags(tag_set));
            const auto& tags = buffer.get<osmium::Node>(offset).tags();
            REQUIRE(filter.matches(tags) == expression.matches(tags));
            REQUIRE(copied.matches(tags) == expression.matches(tags));
        }
    }
}

TEST_CASE("Adding mixed expressions switches atomic-OR filters to general evaluation", "[tags-filter-bool]") {
    const tags_filter_bool::Expression atomic{"a OR b"};
    const tags_filter_bool::Expression conjunction{"c AND d"};
    const tags_filter_bool::Expression negation{"NOT e"};
    const tags_filter_bool::Expression later_atomic{"f"};
    tags_filter_bool::Filter filter;
    filter.add(atomic);
    filter.add(conjunction);
    filter.add(negation);
    filter.add(later_atomic);
    const auto copied = filter;
    auto context = filter.make_context();
    for (unsigned int assignment = 0; assignment < 64; ++assignment) {
        std::vector<std::pair<std::string, std::string>> tags;
        for (unsigned int variable = 0; variable < 6; ++variable) {
            if ((assignment & (1U << variable)) != 0) {
                tags.emplace_back(std::string(1, static_cast<char>('a' + variable)), "yes");
            }
        }
        osmium::memory::Buffer buffer{1024};
        const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tags(tags));
        const auto& tag_list = buffer.get<osmium::Node>(offset).tags();
        const bool expected = (assignment & 3U) != 0 || (assignment & 12U) == 12U ||
                              (assignment & 16U) == 0 || (assignment & 32U) != 0;
        REQUIRE(filter.matches(tag_list) == expected);
        REQUIRE(copied.matches(tag_list) == expected);
        REQUIRE(filter.matches(tag_list, context) == expected);
    }
}

TEST_CASE("Boolean filters intern decoded predicates without merging different atomic semantics", "[tags-filter-bool]") {
    tags_filter_bool::Filter filter;
    filter.add(tags_filter_bool::Expression{"(key=one AND name) OR (key=one AND NOT name)"});
    REQUIRE(filter.predicate_count() == 2);
    filter.add(tags_filter_bool::Expression{"key=one"});
    filter.add(tags_filter_bool::Expression{"NOT key=one"});
    REQUIRE(filter.predicate_count() == 2);
    filter.add(tags_filter_bool::Expression{"key!=one"});
    REQUIRE(filter.predicate_count() == 3);
    filter.add(tags_filter_bool::Expression{"name='Rock AND Roll'"});
    filter.add(tags_filter_bool::Expression{"name=\"Rock AND Roll\""});
    REQUIRE(filter.predicate_count() == 4);

    tags_filter_bool::Filter atomic;
    atomic.add(tags_filter_bool::Expression{"key=one OR key=one OR name"});
    atomic.add(tags_filter_bool::Expression{"name OR key=one"});
    REQUIRE(atomic.predicate_count() == 2);
}

TEST_CASE("Predicate contexts do not retain results across matching invocations or filter plans", "[tags-filter-bool]") {
    const tags_filter_bool::Expression expression{"(a AND b) OR (NOT a AND c)"};
    const tags_filter_bool::Expression other_expression{"NOT c OR (NOT b AND a)"};
    tags_filter_bool::Filter filter;
    tags_filter_bool::Filter other;
    filter.add(expression);
    other.add(other_expression);
    REQUIRE(filter.predicate_count() == other.predicate_count());
    const auto copied = filter;
    auto context = filter.make_context();
    auto independent = filter.make_context();
    auto copied_context = context;
    osmium::memory::Buffer buffer{1024};
    const auto* const address = buffer.data();
    for (unsigned int round = 0; round < 3; ++round) {
        for (unsigned int assignment = 0; assignment < 8; ++assignment) {
            std::vector<std::pair<std::string, std::string>> tags;
            for (unsigned int variable = 0; variable < 3; ++variable) {
                if ((assignment & (1U << variable)) != 0) {
                    tags.emplace_back(std::string(1, static_cast<char>('a' + variable)), "yes");
                }
            }
            buffer.clear();
            const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_id(1), osmium::builder::attr::_tags(tags));
            const auto& tag_list = buffer.get<osmium::Node>(offset).tags();
            REQUIRE(buffer.data() == address);
            const bool expected = expression.matches(tag_list);
            REQUIRE(filter.matches(tag_list, context) == expected);
            REQUIRE(other.matches(tag_list, context) == other_expression.matches(tag_list));
            REQUIRE(copied.matches(tag_list, context) == expected);
            REQUIRE(filter.matches(tag_list, independent) == expected);
            REQUIRE(filter.matches(tag_list, copied_context) == expected);
        }
    }
}

TEST_CASE("Missing or stale predicate contexts safely fall back without resizing", "[tags-filter-bool]") {
    tags_filter_bool::Filter filter;
    filter.add(tags_filter_bool::Expression{"key=one AND NOT key=two"});
    auto context = filter.make_context();
    tags_filter_bool::Filter::Context unprepared;
    filter.add(tags_filter_bool::Expression{"other AND NOT absent"});
    osmium::memory::Buffer buffer{1024};
    const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tag("other", "yes"));
    const auto& tags = buffer.get<osmium::Node>(offset).tags();
    REQUIRE(filter.matches(tags));
    REQUIRE(filter.matches(tags, context));
    REQUIRE(filter.matches(tags, unprepared));
    context = filter.make_context();
    REQUIRE(filter.matches(tags, context));
}

TEST_CASE("Cached predicates retain existential matching with duplicate keys", "[tags-filter-bool]") {
    const tags_filter_bool::Expression expression{"(key=one AND key=two) OR (NOT key=one AND key!=one)"};
    tags_filter_bool::Filter filter;
    filter.add(expression);
    REQUIRE(filter.predicate_count() == 3);
    auto context = filter.make_context();
    const std::vector<std::vector<std::pair<std::string, std::string>>> tag_sets{
        {}, {{"key", ""}}, {{"key", "one"}}, {{"key", "two"}},
        {{"key", "one"}, {"key", "two"}}, {{"unrelated", "yes"}}
    };
    for (const auto& tag_set : tag_sets) {
        osmium::memory::Buffer buffer{1024};
        const auto offset = osmium::builder::add_node(buffer, osmium::builder::attr::_tags(tag_set));
        const auto& tags = buffer.get<osmium::Node>(offset).tags();
        REQUIRE(filter.matches(tags, context) == expression.matches(tags));
    }
}
