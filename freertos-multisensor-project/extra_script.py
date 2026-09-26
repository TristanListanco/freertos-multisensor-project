Import("env")
import os

framework_dir = env.PioPlatform().get_package_dir("framework-stm32cubef1")
freertos_dir = os.path.join(framework_dir, "Middlewares", "Third_Party", "FreeRTOS", "Source")

# Calculate the absolute path to your project's include directory
project_include_dir = os.path.join(env.get("PROJECT_DIR"), "include")

env.Append(CPPPATH=[
    os.path.join(freertos_dir, "include"),
    os.path.join(freertos_dir, "portable", "GCC", "ARM_CM3"),
    project_include_dir  # Forces the compiler to look here for FreeRTOSConfig.h
])

env.BuildSources(
    os.path.join("$BUILD_DIR", "FreeRTOS"),
    freertos_dir,
    src_filter=[
        "+<*.c>",
        "+<portable/GCC/ARM_CM3/*.c>",
        "+<portable/MemMang/heap_4.c>"
    ]
)