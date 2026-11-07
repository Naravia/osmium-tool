// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef COMMAND_GET_PARENTS_EXTENDED_HPP
#define COMMAND_GET_PARENTS_EXTENDED_HPP

#include "cmd.hpp"
#include "reverse_references/parent_lookup.hpp"

#include <string>
#include <vector>

class CommandGetParentsExtended : public CommandWithSingleOSMInput, public with_osm_output {
    reverse_references::ObjectIds m_seeds;
    reverse_references::ParentFilter m_parent_filter;
    std::string m_relationships;
    bool m_add_self = false;

public:
    explicit CommandGetParentsExtended(const CommandFactory& factory) :
        CommandWithSingleOSMInput(factory) {
    }

    bool setup(const std::vector<std::string>& arguments) override final;
    void show_arguments() override final;
    bool run() override final;
    const char* name() const noexcept override final { return "get-parents-extended"; }
    const char* synopsis() const noexcept override final {
        return "osmium get-parents-extended [OPTIONS] OSM-FILE [ID...]\n"
               "       osmium get-parents-extended [OPTIONS] OSM-FILE -I SEED-OSM-FILE";
    }
};

#endif // COMMAND_GET_PARENTS_EXTENDED_HPP
