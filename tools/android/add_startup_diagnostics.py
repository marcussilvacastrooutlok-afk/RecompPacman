from pathlib import Path

path = Path("src/ui/windowed_app_main_sdl.cpp")
text = path.read_text()
include_anchor = "#include <cstdlib>\n"
diag_include = """#if defined(__ANDROID__)
#include <android/log.h>
#define PACMAN_DIAG(...) __android_log_print(ANDROID_LOG_ERROR, "PacmanDiag", __VA_ARGS__)
#else
#define PACMAN_DIAG(...) ((void)0)
#endif
"""
if "PACMAN_DIAG" not in text:
    if include_anchor not in text:
        raise SystemExit("Could not find include anchor in windowed_app_main_sdl.cpp")
    text = text.replace(include_anchor, include_anchor + diag_include, 1)

replacements = [
("""int RunWindowedApp(int argc, char** argv) {
  auto remaining = rex::cvar::Init(argc, argv);""",
"""int RunWindowedApp(int argc, char** argv) {
  PACMAN_DIAG("RunWindowedApp entered");
  auto remaining = rex::cvar::Init(argc, argv);"""),
("""    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }""",
"""    if (!app_context.Initialize()) {
      PACMAN_DIAG("SDLWindowedAppContext::Initialize failed");
      return EXIT_FAILURE;
    }
    PACMAN_DIAG("SDLWindowedAppContext initialized");"""),
("""    if (!creator) {
      return EXIT_FAILURE;
    }""",
"""    if (!creator) {
      PACMAN_DIAG("No WindowedApp creator registered for identifier: %s", REX_ANDROID_APP_IDENTIFIER);
      return EXIT_FAILURE;
    }
    PACMAN_DIAG("Creator found for identifier: %s", REX_ANDROID_APP_IDENTIFIER);"""),
("""    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();""",
"""    const bool initialized = app->OnInitialize();
    PACMAN_DIAG("App OnInitialize returned: %d", initialized ? 1 : 0);
    if (initialized) {
      result = app_context.RunMainMessageLoop();
      PACMAN_DIAG("Main message loop returned: %d", result);
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();"""),
]
for old, new in replacements:
    if old in text:
        text = text.replace(old, new, 1)
    elif new not in text:
        raise SystemExit("Could not find expected startup block: " + old.splitlines()[0])
path.write_text(text)
print("Inserted Android logcat startup diagnostics into windowed_app_main_sdl.cpp.")
