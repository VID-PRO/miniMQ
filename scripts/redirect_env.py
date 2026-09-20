Import("env")

import os


PROJECT_BY_ENV = {
    "fader": "Fader",
    "encoder": "Enncoder",
    "artnet": "ArtNet-USB",
}


def _env_project_dir():
    project = PROJECT_BY_ENV.get(env["PIOENV"])
    if not project:
        return env["PROJECT_DIR"]
    return os.path.abspath(os.path.join(env["PROJECT_DIR"], project))


# This PlatformIO core only reads directory options (src_dir, build_dir, ...)
# from the [platformio] section, never from [env:*]. Relocate the source
# directories here per environment so each project folder stays the single
# source of truth. Build output stays in <root>/.pio (config-level
# build_dir/workspace_dir), which the IDE uses to locate build metadata
# (idedata.json).
base = _env_project_dir()
env.Replace(
    PROJECT_SRC_DIR=os.path.join(base, "src"),
    PROJECT_INCLUDE_DIR=os.path.join(base, "include"),
    PROJECT_TEST_DIR=os.path.join(base, "test"),
    PROJECT_DATA_DIR=os.path.join(base, "data"),
    LIBSOURCE_DIRS=[
        os.path.join(base, "lib"),
        os.path.join(env["PROJECT_LIBDEPS_DIR"], "${PIOENV}"),
        env["PROJECT_CORE_DIR"] + os.path.sep + "lib",
    ],
)

print("redirect_env.py: %s -> %s" % (env["PIOENV"], base))