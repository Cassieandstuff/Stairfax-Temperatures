/* Force-included into every mirror TU (-include mirror_prelude.h).
 *
 * Supplies what rewritten code needs without inserting lines into generated files
 * (keeping them line-preserving: line N of a mirror file == line N of its decomp
 * source, so rules and diagnostics line up):
 *   - <stdint.h>: uintptr_t / int32_t / uint32_t used by pointer-width rewrites
 *   - the portable runtime accessors that pipeline rewrites call
 */
#ifndef STAIRFAX_MIRROR_PRELUDE_H
#define STAIRFAX_MIRROR_PRELUDE_H
#include <stdint.h>
#include "stairfax_os.h"     /* os_globals_read_u32: [[osglobals.read]] rewrites */
#include "stairfax_seqpair.h" /* seqPairTableLookupPtr: [[call.retarget]] rewrites */
#include "stairfax_model.h"   /* stairfax_model_load_unpacked: model_hoststruct.toml */
#endif
