// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TAGS_FILTER_BOOL_ID_SET_HPP
#define TAGS_FILTER_BOOL_ID_SET_HPP

#include <osmium/index/id_set.hpp>
#include <osmium/osm/types.hpp>

namespace tags_filter_bool {

// Keep the existing bitmap semantics, but allocate 1 KiB per occupied chunk
// (8,192 numeric IDs), not libosmium's default 4 MiB. Regional selections can
// contain sparse references across a large global ID range. This tuning is
// local to the Boolean extension; no library or original command default changes.
using ObjectIdSet = osmium::index::IdSetDense<osmium::unsigned_object_id_type, 10>;

} // namespace tags_filter_bool

#endif // TAGS_FILTER_BOOL_ID_SET_HPP
