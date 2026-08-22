#ifndef STDBOOL_H
#define STDBOOL_H

/* Use _Bool directly (GCC built-in) rather than typedef to avoid
 * "cannot be defined via typedef" on some toolchain variants. */
#ifndef bool
#define bool    _Bool
#endif
#ifndef true
#define true    1
#endif
#ifndef false
#define false   0
#endif

#define __bool_true_false_are_defined 1

#endif
