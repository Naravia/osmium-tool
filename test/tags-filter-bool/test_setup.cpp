
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
    SECTION("file syntax errors have filename and line") {
        try {
            cmd.setup({"test/tags-filter/input.osm", "-e", "test/tags-filter-bool/invalid-expressions.txt", "-f", "opl"});
            FAIL("Expected a syntax error");
        } catch (const argument_error& e) {
            REQUIRE(std::string{e.what()}.find("invalid-expressions.txt:3: Boolean expression, column 6:") != std::string::npos);
        }
    }
}
