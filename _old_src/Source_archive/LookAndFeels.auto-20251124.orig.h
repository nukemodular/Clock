#pragma once

// Backwards compatibility shim: include the new consolidated UiTheme header.
// Historically this file provided shared LookAndFeel and theme colours. The
// implementation has been moved to Source/UiTheme.h — keep this shim so
// other small headers including LookAndFeels.h continue to work.
#include "UiTheme.h"
