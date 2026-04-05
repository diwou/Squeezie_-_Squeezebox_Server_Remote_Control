Import("env")
import subprocess
import os

def generate_compiledb(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    subprocess.run(
        ["pio", "run", "-t", "compiledb"],
        cwd=project_dir,
        capture_output=True,
    )

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", generate_compiledb)
