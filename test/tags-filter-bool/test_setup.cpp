
#include "test.hpp" // IWYU pragma: keep

#include "command_tags_filter_bool.hpp"

#include <string>

TEST_CASE("tags-filter-bool setup", "[tags-filter-bool]") {
    const CommandFactory factory;
    CommandTagsFilterBool cmd{factory};

    SECTION("help") {
        REQUIRE_FALSE(cmd.setup({"--help"}));
    }
    SECTION("missing input") {
        REQUIRE_THROWS_AS(cmd.setup({}), argument_error);
    }
    SECTION("missing expressions") {
        REQUIRE_THROWS_AS(cmd.setup({"test/tags-filter/input.osm", "-f", "opl"}), argument_error);
    }
    SECTION("invalid expression") {
        REQUIRE_THROWS_AS(cmd.setup({"test/tags-filter/input.osm", "a AND", "-f", "opl"}), argument_error);
    }
    SECTION("missing expression file") {
        REQUIRE_THROWS_AS(cmd.setup({"test/tags-filter/input.osm", "-e", "test/tags-filter-bool/missing.txt", "-f", "opl"}), argument_error);
    }
    SECTION("STDIN requires omit-referenced") {
        REQUIRE_THROWS_AS(cmd.setup({"-", "a", "-F", "osm", "-f", "opl"}), argument_error);
    }
    SECTION("STDIN with omit-referenced") {
        REQUIRE(cmd.setup({"-", "NOT a", "-R", "-F", "osm", "-f", "opl"}));
    }
    SECTION("multiple alternatives and common flags") {
        REQUIRE(cmd.setup({"test/tags-filter/input.osm", "n/amenity AND NOT name", "w/highway", "-i", "-t", "-R", "-f", "opl"}));
    }
    SECTION("downward relation hops without parent lookup") {
        const auto hops = GENERATE("0", "1", "2", "0002", "18446744073709551615");
        // The largest value is valid only if size_t can represent it.
        if (std::string{hops} != "18446744073709551615" || sizeof(std::size_t) >= 8) {
            REQUIRE(cmd.setup({"test/tags-filter/input.osm", "r/type", "--downward-relation-hops", hops, "-f", "opl"}));
        }
    }
    SECTION("invalid downward relation hops") {
        const auto hops = GENERATE("", "-1", "+1", "1.0", " 1", "1 ", "one", "18446744073709551616");
        REQUIRE_THROWS_AS(cmd.setup({"test/tags-filter/input.osm", "r/type", "--downward-relation-hops", hops, "-f", "opl"}), argument_error);
    }
    SECTION("downward limit conflicts with omit-referenced, including zero") {
        const auto hops = GENERATE("0", "1");
        const auto omit = GENERATE("-R", "--omit-referenced");
        REQUIRE_THROWS_AS(cmd.setup({"test/tags-filter/input.osm", "r/type", "--downward-relation-hops", hops, omit, "-f", "opl"}), argument_error);
    }
    SECTION("bounded completion still requires replayable input") {
        REQUIRE_THROWS_AS(cmd.setup({"-", "r/type", "--downward-relation-hops=0", "-F", "opl", "-f", "opl"}), argument_error);
    }
    SECTION("file syntax errors have filename and line") {
        try {
            cmd.setup({"test/tags-filter/input.osm", "-e", "test/tags-filter-bool/invalid-expressions.txt", "-f", "opl"});
            FAIL("Expected a syntax error");
        } catch (const argument_error& e) {
            REQUIRE(std::string{e.what()}.find("invalid-expressions.txt:3: Boolean expression, column 6:") != std::string::npos);
        }
    }
}
