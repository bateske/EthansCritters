// The squire's progress. See Save.h.
#pragma GCC optimize("Os")
#include "src/Size.h"       // (first: Size.h)
#include <CHGame.h>
#include "Save.h"

namespace progress {

static const uint32_t MAGIC = save::magic("ECRT");
static const uint8_t VERSION = 2;           // Record's shape (1: before the fog, M7b; M8's card mark took
                                            // two bytes of padding, so a version 2 record from before still loads)

static_assert(sizeof(Record) <= save::MAX_DATA, "the record must fit a save page");

const Record *load() { return (const Record *)save::read(MAGIC, VERSION, sizeof(Record)); }
Record &edit() { return *(Record *)save::buffer(); }
bool store() { return save::write(MAGIC, VERSION, sizeof(Record)); }

}  // namespace progress
