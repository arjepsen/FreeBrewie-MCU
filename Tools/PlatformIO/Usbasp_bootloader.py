"""
PlatformIO custom target for installing the Brewie ATmega2560 bootloader over USBasp.

Responsibility:
    Provide one clear VS Code/PlatformIO recovery button for the Brewie MCU.

Why this exists:
    PlatformIO's built-in "Burn Bootloader" target runs avrdude twice and also
    applies the stock final lock byte. Hardware testing showed two Brewie-specific
    requirements:
    - USBasp must use a slow ISP clock on this VM/setup.
    - final lock byte 0x0F prevents the FreeBrewie app from starting here.

    This target installs the old Brewie-carried STK500v2 bootloader image,
    leaves lock bits open, and keeps reset directed to the bootloader. Hardware
    testing showed that this still starts the FreeBrewie app after the bootloader
    timeout, while also allowing SOM-side UART updates.
"""

from os.path import abspath, join

from SCons.Script import AlwaysBuild, DefaultEnvironment


env = DefaultEnvironment()
platform = env.PioPlatform()
project_dir = env.subst("$PROJECT_DIR")

avrdude_conf_path = join(platform.get_package_dir("tool-avrdude"), "avrdude.conf")
bootloader_path = abspath(join(
    project_dir,
    "Bootloaders",
    "stk500v2",
    "stk500boot_v2_mega2560_brewie.hex",
))

restore_bootloader_command = [
    "avrdude",
    "-p",
    "atmega2560",
    "-C",
    avrdude_conf_path,
    "-c",
    "usbasp",
    "-B",
    "100",
    "-e",
    "-Ulock:w:0x3F:m",
    "-Uhfuse:w:0xD8:m",
    "-Ulfuse:w:0xFF:m",
    "-Uefuse:w:0xFD:m",
    "-Uflash:w:%s:i" % bootloader_path,
]

restore_bootloader_target = env.AddCustomTarget(
    "restore_bootloader_usbasp",
    None,
    env.VerboseAction(
        " ".join(restore_bootloader_command),
        "Installing Brewie ATmega2560 bootloader with USBasp",
    ),
    title="Restore Bootloader USBasp",
    description="Install the Brewie STK500v2 bootloader with USBasp, leaving SOM flashing enabled.",
)

AlwaysBuild(restore_bootloader_target)
