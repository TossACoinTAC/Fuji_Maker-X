if [[ -n "${BASH_VERSION:-}" ]]; then
    echo "Maker-X ESP-IDF environment must be activated from zsh."
    return 1
fi

idf_toolchain_root="/Volumes/Mac_DiskExtension/Developer/Toolchains"
if [[ -f "$idf_toolchain_root/esp/esp-idf-v6.0.2/export.sh" ]]; then
    export IDF_PATH="$idf_toolchain_root/esp/esp-idf-v6.0.2"
    export IDF_TOOLS_PATH="$idf_toolchain_root/espressif"
else
    export IDF_PATH="$HOME/esp/esp-idf-v6.0.2"
    unset IDF_TOOLS_PATH
fi
unset IDF_PYTHON_ENV_PATH

if [[ -z "${XIAOZHI_BUILD_ROOT:-}" && -d "/Volumes/Mac_DiskExtension" ]]; then
    export XIAOZHI_BUILD_ROOT="/Volumes/Mac_DiskExtension/EmbeddedCache/Maker-X/xiaozhi"
fi

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF v6.0.2 is not available at $IDF_PATH. Mount Mac_DiskExtension or install the pinned toolchain locally."
    return 1
fi

source "$IDF_PATH/export.sh"
unset idf_toolchain_root
