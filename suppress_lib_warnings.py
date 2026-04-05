Import("env")

def suppress_warnings(env, node):
    return env.Object(
        node,
        CCFLAGS=[f for f in env["CCFLAGS"] if f not in ["-Wall", "-Wextra"]] + ["-w"],
    )

env.AddBuildMiddleware(suppress_warnings, "**/LovyanGFX/**")
