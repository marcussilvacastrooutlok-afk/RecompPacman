from pathlib import Path

path = Path("src/ui/rex_app.cpp")
text = path.read_text()
include_anchor = "#include <cstdlib>\n"
sdl_include = "#include <SDL3/SDL.h>\n"
if sdl_include not in text:
    if include_anchor not in text:
        raise SystemExit("Could not find SDL include insertion point in src/ui/rex_app.cpp")
    text = text.replace(include_anchor, include_anchor + sdl_include, 1)

old = (
    '  if (log_file_cvar.empty()) {\n'
    '    log_config.app_name = std::string(GetName());\n'
    '    log_config.log_dir = (exe_dir / "logs").string();\n'
    '  }\n'
)
new = (
    '  if (log_file_cvar.empty()) {\n'
    '#if defined(__ANDROID__)\n'
    '    // Android executables live under /system/bin, which is read-only.\n'
    '    // Use SDL\'s private, writable per-app preference directory for logs.\n'
    '    std::string app_name(GetName());\n'
    '    char* pref_path = SDL_GetPrefPath("ReXGlue", app_name.c_str());\n'
    '    if (pref_path) {\n'
    '      log_config.app_name = app_name;\n'
    '      log_config.log_dir = (std::filesystem::path(pref_path) / "logs").string();\n'
    '      SDL_free(pref_path);\n'
    '    } else {\n'
    '      // Logging must never abort game startup if a writable path is unavailable.\n'
    '      log_config.app_name.clear();\n'
    '      log_config.log_dir.clear();\n'
    '    }\n'
    '#else\n'
    '    log_config.app_name = std::string(GetName());\n'
    '    log_config.log_dir = (exe_dir / "logs").string();\n'
    '#endif\n'
    '  }\n'
)
if old not in text:
    raise SystemExit("Could not find original log path block; no change applied.")
path.write_text(text.replace(old, new, 1))
print("Android log directory now uses SDL_GetPrefPath.")
