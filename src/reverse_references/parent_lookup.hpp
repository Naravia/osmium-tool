// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REVERSE_REFERENCES_PARENT_LOOKUP_HPP
#define REVERSE_REFERENCES_PARENT_LOOKUP_HPP

// Extension-local reverse membership lookup. No state is added to libosmium.
#include "../tags_filter_bool/expression.hpp"

#include <osmium/index/nwr_array.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/header.hpp>
#include <osmium/osm.hpp>

#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace reverse_references {

class ObjectIds {
    osmium::nwr_array<std::unordered_set<osmium::unsigned_object_id_type>> m_ids;

public:
    bool add(osmium::item_type type, osmium::object_id_type id);
    bool contains(osmium::item_type type, osmium::object_id_type id) const noexcept;
    bool empty() const noexcept;
    bool has(osmium::item_type type) const noexcept;
    std::size_t size(osmium::item_type type) const noexcept;
    osmium::osm_entity_bits::type types() const noexcept;
};

// A separate predicate/context plan for the parent, never the seed predicate.
class ParentFilter {
    osmium::nwr_array<tags_filter_bool::Filter> m_filters;
    osmium::nwr_array<tags_filter_bool::Filter::Context> m_contexts;
    tags_filter_bool::Filter m_areas;
    tags_filter_bool::Filter::Context m_area_context;
    bool m_configured = false;

public:
    void setup(const std::vector<std::string>& sources);
    osmium::osm_entity_bits::type types() const noexcept;
    bool matches(const osmium::OSMObject& object) noexcept;
};

// Occurrence-level TSV; deliberately independent of OSM output serialization.
class Relationships {
    std::ofstream m_output;

public:
    explicit Relationships(const std::string& filename);
    bool enabled() const noexcept;
    void write(osmium::item_type child_type, osmium::object_id_type child,
               const osmium::OSMObject& parent, const char* role, std::size_t index);
    void close();
};

// seeds must remain unchanged throughout a scan. Found parents are not seeds.
class ParentLookup {
    const ObjectIds& m_seeds;
    ParentFilter& m_filter;

public:
    ParentLookup(const ObjectIds& seeds, ParentFilter& filter) :
        m_seeds(seeds), m_filter(filter) {
    }

    osmium::osm_entity_bits::type types() const noexcept;
    bool matches(const osmium::OSMObject& parent, Relationships* relationships = nullptr);
};

struct ParentSearch {
    ObjectIds parents;
    std::size_t passes = 0;
};

// Each level uses a frozen frontier. Parent filters prune traversal at every
// level; already visited parents can still contribute relationship edges.
ParentSearch find_parents(const osmium::io::File& file, const ObjectIds& seeds,
                          ParentFilter& filter, std::size_t hops, Relationships* relationships = nullptr);
std::size_t parse_parent_hops(const std::string& value);

void require_snapshot(const osmium::io::File& file, const osmium::io::Header& header);
void read_seed_file(const std::string& filename, ObjectIds& ids);
void check_distinct_output(const std::string& filename, const std::vector<std::string>& protected_files);
void check_relationship_path(const std::string& filename, const std::vector<std::string>& protected_files, bool overwrite);

} // namespace reverse_references

#endif // REVERSE_REFERENCES_PARENT_LOOKUP_HPP
