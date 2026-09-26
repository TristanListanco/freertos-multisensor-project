Import("env")
import os

framework_dir = env.PioPlatform().get_package_dir("framework-stm32cubef1")
freertos_dir = os.path.join(framework_dir, "Middlewares", "Third_Party", "FreeRTOS", "Source")

# Calculate the absolute path to your project's include directory
project_include_dir = os.path.join(env.get("PROJECT_DIR"), "include")

# Use the ARM_CM0 port even though the Blue Pill is a Cortex-M3. The CM3 port
# starts the first task with an SVC, which the Wokwi Blue Pill model ignores, so
# the scheduler falls into prvTaskExitError before any task runs. The CM0 port
# jumps straight into the first task, and its ARMv6-M code runs unchanged on
# the M3.
freertos_port = os.path.join("portable", "GCC", "ARM_CM0")

env.Append(CPPPATH=[
    os.path.join(freertos_dir, "include"),
    os.path.join(freertos_dir, freertos_port),
    project_include_dir  # Forces the compiler to look here for FreeRTOSConfig.h
])

# Route the kernel's yields to __wrap_vPortYield in src/wokwi_port_fixes.c.
env.Append(LINKFLAGS=["-Wl,--wrap=vPortYield"])

env.BuildSources(
    os.path.join("$BUILD_DIR", "FreeRTOS"),
    freertos_dir,
    src_filter=[
        "+<*.c>",
        "+<%s/*.c>" % freertos_port.replace(os.sep, "/"),
        "+<portable/MemMang/heap_4.c>"
    ]
)