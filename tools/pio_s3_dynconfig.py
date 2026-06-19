Import("env")

dynconfig_by_env = {
    "esp32dev": "xtensa_esp32.so",
    "esp32s3dev": "xtensa_esp32s3.so",
}

dynconfig = dynconfig_by_env.get(env["PIOENV"])

if dynconfig:
    import os
    import shutil

    toolchain_dir = env.PioPlatform().get_package_dir("toolchain-xtensa-esp-elf")
    src = os.path.join(toolchain_dir or "", "lib", dynconfig)
    dst = os.path.join(env.subst("$PROJECT_DIR"), dynconfig)
    if os.path.exists(src):
        if (not os.path.exists(dst) or
                os.path.getmtime(src) > os.path.getmtime(dst) or
                os.path.getsize(src) != os.path.getsize(dst)):
            shutil.copy2(src, dst)

    env.Append(
        CCFLAGS=[f"-mdynconfig={dynconfig}"],
        CXXFLAGS=[f"-mdynconfig={dynconfig}"],
        ASFLAGS=[f"--dynconfig={dynconfig}"],
        LINKFLAGS=[
            f"-mdynconfig={dynconfig}",
            "-Wl,-EL",
        ],
    )
