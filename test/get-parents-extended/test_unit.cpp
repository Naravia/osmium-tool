#include "test.hpp"
#include "command_get_parents_extended.hpp"
#include "command_tags_filter_bool.hpp"
#include "reverse_references/parent_lookup.hpp"

#include <osmium/builder/attr.hpp>
#include <osmium/memory/buffer.hpp>

TEST_CASE("Sparse reverse IDs are typed and positive", "[reverse-references]") {
    reverse_references::ObjectIds ids;
    REQUIRE(ids.empty());
    ids.add(osmium::item_type::node, 1);
    ids.add(osmium::item_type::node, 1);
    REQUIRE(ids.size(osmium::item_type::node) == 1);
    REQUIRE(ids.contains(osmium::item_type::node, 1));
    REQUIRE_FALSE(ids.contains(osmium::item_type::way, 1));
    REQUIRE_FALSE(ids.contains(osmium::item_type::node, -1));
    REQUIRE_THROWS_AS(ids.add(osmium::item_type::node, -1), argument_error);
    REQUIRE_THROWS_AS(ids.add(osmium::item_type::way, 0), argument_error);
    REQUIRE_THROWS_AS(ids.add(osmium::item_type::changeset, 1), argument_error);
}

TEST_CASE("Shared parent lookup uses immutable typed seeds, not discovered parents", "[reverse-references]") {
    using namespace osmium::builder::attr;
    reverse_references::ObjectIds seeds;
    seeds.add(osmium::item_type::node, 1);
    reverse_references::ParentFilter filter;
    reverse_references::ParentLookup lookup{seeds, filter};
    osmium::memory::Buffer buffer{4096};
    const auto way = osmium::builder::add_way(buffer, _id(10), _nodes({1, 2, 1}));
    const auto wrong_type = osmium::builder::add_relation(buffer, _id(20), _member(osmium::item_type::way, 1));
    const auto direct = osmium::builder::add_relation(buffer, _id(21), _member(osmium::item_type::node, 1, "label"));
    const auto ancestor = osmium::builder::add_relation(buffer, _id(22), _member(osmium::item_type::way, 10));
    REQUIRE(lookup.matches(buffer.get<osmium::Way>(way)));
    REQUIRE_FALSE(lookup.matches(buffer.get<osmium::Relation>(wrong_type)));
    REQUIRE(lookup.matches(buffer.get<osmium::Relation>(direct)));
    REQUIRE_FALSE(lookup.matches(buffer.get<osmium::Relation>(ancestor)));
    REQUIRE(seeds.size(osmium::item_type::node) == 1);
    REQUIRE_FALSE(seeds.has(osmium::item_type::way));
}

TEST_CASE("Parent predicates have independent Boolean and area contexts", "[reverse-references]") {
    using namespace osmium::builder::attr;
    reverse_references::ObjectIds seeds;
    seeds.add(osmium::item_type::node, 1);
    reverse_references::ParentFilter filter;
    filter.setup({"a/(building AND NOT building=construction)"});
    reverse_references::ParentLookup lookup{seeds, filter};
    osmium::memory::Buffer buffer{4096};
    const auto closed = osmium::builder::add_way(buffer, _id(10), _nodes({1, 2, 3, 1}), _tag("building", "yes"));
    const auto open = osmium::builder::add_way(buffer, _id(11), _nodes({1, 2, 3}), _tag("building", "yes"));
    const auto excluded = osmium::builder::add_way(buffer, _id(12), _nodes({1, 2, 3, 1}), _tag("building", "construction"));
    const auto relation = osmium::builder::add_relation(buffer, _id(20), _member(osmium::item_type::node, 1), _tags({{"type", "multipolygon"}, {"building", "yes"}}));
    REQUIRE(lookup.matches(buffer.get<osmium::Way>(closed)));
    REQUIRE_FALSE(lookup.matches(buffer.get<osmium::Way>(open)));
    REQUIRE_FALSE(lookup.matches(buffer.get<osmium::Way>(excluded)));
    REQUIRE(lookup.matches(buffer.get<osmium::Relation>(relation)));
    REQUIRE(lookup.matches(buffer.get<osmium::Way>(closed)));
}

TEST_CASE("Parent command setup and option restrictions", "[reverse-references]") {
    const CommandFactory factory;
    SECTION("help") {
        CommandGetParentsExtended command{factory};
        REQUIRE_FALSE(command.setup({"--help"}));
    }
    SECTION("missing seeds") {
        CommandGetParentsExtended command{factory};
        REQUIRE_THROWS_AS(command.setup({"input.opl", "-f", "opl"}), argument_error);
    }
    SECTION("negative ID") {
        CommandGetParentsExtended command{factory};
        REQUIRE_THROWS_AS(command.setup({"input.opl", "n-1", "-f", "opl"}), argument_error);
    }
    SECTION("invalid default type") {
        CommandGetParentsExtended command{factory};
        REQUIRE_THROWS_AS(command.setup({"input.opl", "1", "--default-type=x", "-f", "opl"}), argument_error);
    }
    SECTION("invalid parent predicate") {
        CommandGetParentsExtended command{factory};
        REQUIRE_THROWS_AS(command.setup({"input.opl", "n1", "--parents-expression=NOT", "-f", "opl"}), argument_error);
    }
    SECTION("parent options require opt in") {
        CommandTagsFilterBool command{factory};
        REQUIRE_THROWS_AS(command.setup({"input.opl", "key", "--parents-expression=r/type=site", "-f", "opl"}), argument_error);
    }
    SECTION("integrated parent lookup cannot replay STDIN") {
        CommandTagsFilterBool command{factory};
        REQUIRE_THROWS_AS(command.setup({"-", "key", "--include-parents", "-R", "-F", "opl", "-f", "opl"}), argument_error);
    }
    SECTION("cannot overwrite source through relationship sidecar") {
        CommandGetParentsExtended command{factory};
        REQUIRE_THROWS_AS(command.setup({"test/tags-filter-bool/input.osm.opl", "n1", "--parent-relationships=test/tags-filter-bool/input.osm.opl", "-O", "-f", "opl"}), argument_error);
    }
    SECTION("cannot overwrite integrated input") {
        CommandTagsFilterBool command{factory};
        REQUIRE_THROWS_AS(command.setup({"test/tags-filter-bool/input.osm.opl", "key", "--include-parents", "-o", "test/tags-filter-bool/input.osm.opl", "-O"}), argument_error);
    }
}
