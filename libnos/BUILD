cc_library(
    name = "libnos",
    srcs = [
        "NuggetClient.cpp",
        "debug.cpp",
        "feature.cpp",
    ],
    hdrs = [
        "include/nos/AppClient.h",
        "include/nos/NuggetClient.h",
        "include/nos/NuggetClientInterface.h",
        "include/nos/debug.h",
        "include/nos/feature.h",
    ],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//host/generic:nos_headers",
        "//host/generic/libnos_transport",
    ],
)

cc_library(
    name = "libnos_debuggable",
    srcs = [
        "NuggetClientDebuggable.cpp",
    ],
    hdrs = [
        "include/nos/NuggetClient.h",
        "include/nos/NuggetClientDebuggable.h",
    ],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//host/generic:nos_headers",
        "//host/generic/libnos",
    ],
)
