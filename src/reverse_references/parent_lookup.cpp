// SPDX-License-Identifier: GPL-3.0-or-later
#include "parent_lookup.hpp"

#include "../exception.hpp"

#include <osmium/io/reader.hpp>
#include <osmium/memory/buffer.hpp>

#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

namespace reverse_references {

namespace {

bool is_object(osmium::item_type type) noexcept {
    return type == osmium::item_type::node || type == osmium::item_type::way || type == osmium::item_type::relation;
}

void positive(osmium::object_id_type id) {
    if (id <= 0) {
        throw argument_error{"Parent lookup requires positive OSM IDs and snapshot input."};
    }
}

bool is_area(const osmium::Relation& relation) noexcept {
    const char* type = relation.tags().get_value_by_key("type");
    return type && (!std::strcmp(type, "multipolygon") || !std::strcmp(type, "boundary"));
}

std::string escape_role(const char* role) {
    std::string result;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(role); *p; ++p) {
        if (*p == '%' || *p == '\t' || *p == '\r' || *p == '\n') {
            constexpr char hex[] = "0123456789ABCDEF";
            result += '%';
            result += hex[*p >> 4U];
            result += hex[*p & 15U];
        } else {
            result += static_cast<char>(*p);
        }
    }
    return result;
}

} // namespace

bool ObjectIds::add(osmium::item_type type, osmium::object_id_type id) {
    if (!is_object(type)) {
        throw argument_error{"Expected a node, way, or relation ID."};
    }
    positive(id);
    return m_ids(type).insert(static_cast<osmium::unsigned_object_id_type>(id)).second;
}

bool ObjectIds::contains(osmium::item_type type, osmium::object_id_type id) const noexcept {
    return is_object(type) && id > 0 && m_ids(type).find(static_cast<osmium::unsigned_object_id_type>(id)) != m_ids(type).end();
}

bool ObjectIds::empty() const noexcept {
    return !has(osmium::item_type::node) && !has(osmium::item_type::way) && !has(osmium::item_type::relation);
}

bool ObjectIds::has(osmium::item_type type) const noexcept {
    return !m_ids(type).empty();
}

std::size_t ObjectIds::size(osmium::item_type type) const noexcept {
    return m_ids(type).size();
}

osmium::osm_entity_bits::type ObjectIds::types() const noexcept {
    auto result = osmium::osm_entity_bits::nothing;
    for (const auto type : {osmium::item_type::node, osmium::item_type::way, osmium::item_type::relation}) {
        if (has(type)) {
            result |= osmium::osm_entity_bits::from_item_type(type);
        }
    }
    return result;
}

void ParentFilter::setup(const std::vector<std::string>& sources) {
    m_configured = !sources.empty();
    for (const auto& source : sources) {
        const tags_filter_bool::Expression expression{source};
        for (const auto type : {osmium::item_type::way, osmium::item_type::relation}) {
            if (expression.entities() & osmium::osm_entity_bits::from_item_type(type)) {
                m_filters(type).add(expression);
            }
        }
        if (expression.entities() & osmium::osm_entity_bits::area) {
            m_areas.add(expression);
        }
    }
    for (const auto type : {osmium::item_type::way, osmium::item_type::relation}) {
        m_contexts(type) = m_filters(type).make_context();
    }
    m_area_context = m_areas.make_context();
}

osmium::osm_entity_bits::type ParentFilter::types() const noexcept {
    if (!m_configured) {
        return osmium::osm_entity_bits::way | osmium::osm_entity_bits::relation;
    }
    auto result = osmium::osm_entity_bits::nothing;
    for (const auto type : {osmium::item_type::way, osmium::item_type::relation}) {
        if (!m_filters(type).empty() || !m_areas.empty()) {
            result |= osmium::osm_entity_bits::from_item_type(type);
        }
    }
    return result;
}

bool ParentFilter::matches(const osmium::OSMObject& object) noexcept {
    if (object.type() != osmium::item_type::way && object.type() != osmium::item_type::relation) {
        return false;
    }
    if (!m_configured) {
        return true;
    }
    if (m_filters(object.type()).matches(object.tags(), m_contexts(object.type()))) {
        return true;
    }
    if (m_areas.empty()) {
        return false;
    }
    if (object.type() == osmium::item_type::way) {
        const auto& way = static_cast<const osmium::Way&>(object);
        return way.nodes().size() >= 4 && way.is_closed() && m_areas.matches(way.tags(), m_area_context);
    }
    return is_area(static_cast<const osmium::Relation&>(object)) && m_areas.matches(object.tags(), m_area_context);
}

Relationships::Relationships(const std::string& filename) {
    if (!filename.empty()) {
        m_output.open(filename);
        if (!m_output) {
            throw std::runtime_error{"Could not open parent relationship file '" + filename + "'"};
        }
        m_output << "child\tparent\trole_percent_escaped\tmember_index\n";
    }
}

bool Relationships::enabled() const noexcept {
    return m_output.is_open();
}

void Relationships::write(osmium::item_type child_type, osmium::object_id_type child,
                          const osmium::OSMObject& parent, const char* role, std::size_t index) {
    m_output << osmium::item_type_to_char(child_type) << child << '\t'
             << osmium::item_type_to_char(parent.type()) << parent.id() << '\t'
             << escape_role(role) << '\t' << index << '\n';
}

void Relationships::close() {
    if (enabled()) {
        m_output.close();
        if (!m_output) {
            throw std::runtime_error{"Could not finish parent relationship file"};
        }
    }
}

osmium::osm_entity_bits::type ParentLookup::types() const noexcept {
    if (m_seeds.empty()) {
        return osmium::osm_entity_bits::nothing;
    }
    auto result = m_filter.types();
    if (!m_seeds.has(osmium::item_type::node)) {
        result &= ~osmium::osm_entity_bits::way;
    }
    return result;
}

bool ParentLookup::matches(const osmium::OSMObject& parent, Relationships* relationships) {
    if ((parent.type() != osmium::item_type::way && parent.type() != osmium::item_type::relation) || m_seeds.empty()) {
        return false;
    }
    positive(parent.id());
    if (!m_filter.matches(parent)) {
        return false;
    }
    const bool edges = relationships && relationships->enabled();
    bool found = false;
    std::size_t index = 0;
    if (parent.type() == osmium::item_type::way) {
        for (const auto& nr : static_cast<const osmium::Way&>(parent).nodes()) {
            positive(nr.ref());
            if (m_seeds.contains(osmium::item_type::node, nr.ref())) {
                if (!edges) {
                    return true;
                }
                found = true;
                relationships->write(osmium::item_type::node, nr.ref(), parent, "", index);
            }
            ++index;
        }
    } else {
        for (const auto& member : static_cast<const osmium::Relation&>(parent).members()) {
            positive(member.ref());
            if (m_seeds.contains(member.type(), member.ref())) {
                if (!edges) {
                    return true;
                }
                found = true;
                relationships->write(member.type(), member.ref(), parent, member.role(), index);
            }
            ++index;
        }
    }
    return found;
}

std::size_t parse_parent_hops(const std::string& value) {
    if (value.empty()) throw argument_error{"Parent hops must be a non-negative integer."};
    std::size_t result = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') throw argument_error{"Parent hops must be a non-negative integer."};
        const auto digit = static_cast<std::size_t>(c - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            throw argument_error{"Parent hop count is too large."};
        }
        result = result * 10 + digit;
    }
    return result;
}

ParentSearch find_parents(const osmium::io::File& file, const ObjectIds& seeds,
                          ParentFilter& filter, std::size_t hops, Relationships* relationships) {
    if (hops > 1 && (file.filename().empty() || file.filename() == "-")) {
        throw argument_error{"Multiple parent hops require a replayable source file, not STDIN."};
    }
    ParentSearch result;
    const ObjectIds* frontier = &seeds;
    ObjectIds current;
    for (std::size_t hop = 0; hop < hops && !frontier->empty(); ++hop) {
        ParentLookup lookup{*frontier, filter};
        if (lookup.types() == osmium::osm_entity_bits::nothing) break;
        ObjectIds next;
        ++result.passes;
        osmium::io::Reader reader{file, lookup.types()};
        require_snapshot(file, reader.header());
        while (osmium::memory::Buffer buffer = reader.read()) {
            for (const auto& parent : buffer.select<osmium::OSMObject>()) {
                // Match before the visited check: repeated roles, cross-edges,
                // and cycle-closing edges must not disappear from the sidecar.
                if (lookup.matches(parent, relationships)) {
                    const bool first = result.parents.add(parent.type(), parent.id());
                    if (first && hop + 1 < hops && !seeds.contains(parent.type(), parent.id())) {
                        next.add(parent.type(), parent.id());
                    }
                }
            }
        }
        reader.close();
        current = std::move(next);
        frontier = &current;
    }
    return result;
}

void require_snapshot(const osmium::io::File& file, const osmium::io::Header& header) {
    if (file.has_multiple_object_versions() || header.has_multiple_object_versions()) {
        throw argument_error{"Parent lookup does not support history or change files."};
    }
}

void read_seed_file(const std::string& filename, ObjectIds& ids) {
    const osmium::io::File file{filename};
    osmium::io::Reader reader{file, osmium::osm_entity_bits::nwr};
    require_snapshot(file, reader.header());
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (const auto& object : buffer.select<osmium::OSMObject>()) {
            ids.add(object.type(), object.id());
        }
    }
    reader.close();
}

void check_distinct_output(const std::string& filename, const std::vector<std::string>& protected_files) {
    if (filename.empty() || filename == "-") {
        return;
    }
    namespace fs = std::filesystem;
    const auto target = fs::weakly_canonical(fs::absolute(filename));
    for (const auto& name : protected_files) {
        if (!name.empty() && name != "-") {
            const auto other = fs::weakly_canonical(fs::absolute(name));
            if (target == other || (fs::exists(target) && fs::exists(other) && fs::equivalent(target, other))) {
                throw argument_error{"Output must differ from all input files and other outputs."};
            }
        }
    }
}

void check_relationship_path(const std::string& filename, const std::vector<std::string>& protected_files, bool overwrite) {
    if (filename.empty()) return;
    if (filename == "-") {
        throw argument_error{"Parent relationships require a named file, not STDOUT."};
    }
    check_distinct_output(filename, protected_files);
    namespace fs = std::filesystem;
    const auto target = fs::weakly_canonical(fs::absolute(filename));
    if (!overwrite && fs::exists(target)) {
        throw argument_error{"Parent relationship output already exists (use --overwrite to replace it)."};
    }
}

} // namespace reverse_references
