def _first_existing_path(repository_ctx, candidates):
    for candidate in candidates:
        if repository_ctx.path(candidate).exists:
            return candidate
    return None

def _host_sdbus_cpp_repository_impl(repository_ctx):
    installations = [
        {
            "name": "/usr/local",
            "headers": "/usr/local/include/sdbus-c++",
            "libraries": [
                "/usr/local/lib/libsdbus-c++.so",
                "/usr/local/lib/libsdbus-c++.so.2",
                "/usr/local/lib64/libsdbus-c++.so",
                "/usr/local/lib64/libsdbus-c++.so.2",
            ],
        },
        {
            "name": "/usr",
            "headers": "/usr/include/sdbus-c++",
            "libraries": [
                "/usr/lib/x86_64-linux-gnu/libsdbus-c++.so",
                "/usr/lib/x86_64-linux-gnu/libsdbus-c++.so.2",
                "/usr/lib/libsdbus-c++.so",
                "/usr/lib/libsdbus-c++.so.2",
                "/usr/lib64/libsdbus-c++.so",
                "/usr/lib64/libsdbus-c++.so.2",
            ],
        },
    ]

    for installation in installations:
        header_dir = repository_ctx.path(installation["headers"])
        if not header_dir.exists:
            continue

        library_path = _first_existing_path(repository_ctx, installation["libraries"])
        if library_path == None:
            continue

        repository_ctx.symlink(header_dir, "sdbus-c++")
        repository_ctx.symlink(repository_ctx.path(library_path), "libsdbus-c++.so")
        repository_ctx.file(
            "BUILD.bazel",
            """package(default_visibility = ["//visibility:public"])

load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "sdbus_cpp",
    srcs = ["libsdbus-c++.so"],
    hdrs = glob(["sdbus-c++/**/*.h*", "sdbus-c++/**/*.inl"]),
    includes = ["."],
)

cc_library(
    name = "libsystemd",
    linkopts = ["-lsystemd"],
)
""",
        )
        return

    fail(
        "Unable to locate a usable sdbus-c++ installation. Checked /usr/local " +
        "and /usr for headers under include/sdbus-c++ and a matching " +
        "libsdbus-c++.so runtime."
    )

host_sdbus_cpp_repository = repository_rule(
    implementation = _host_sdbus_cpp_repository_impl,
    local = True,
)
