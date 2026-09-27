Import("env")
from SCons.Script import COMMAND_LINE_TARGETS

# The native environment exists only for `pio test` (unit tests on this
# computer). A plain `pio run` names no target, and there is no host program to
# build, so finish successfully instead of failing with "Nothing to build".
# `pio test` and `pio check` name their own targets and are unaffected.
if not COMMAND_LINE_TARGETS:
    env.Exit(0)
