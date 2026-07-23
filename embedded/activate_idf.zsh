if [[ -n "${BASH_VERSION:-}" ]]; then
    echo "Maker-X ESP-IDF environment must be activated from zsh."
    return 1
fi

export IDF_PATH="$HOME/esp/esp-idf-v6.0.2"
unset IDF_TOOLS_PATH IDF_PYTHON_ENV_PATH

if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo "ESP-IDF v6.0.2 is not installed at $IDF_PATH."
    return 1
fi

source "$IDF_PATH/export.sh"
