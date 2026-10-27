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

# Instrument ReXApp initialization so a false return identifies the failing phase.
app_path = Path("src/ui/rex_app.cpp")
app_text = app_path.read_text()
app_anchor = "#include <cstdlib>\n"
if "PACMAN_DIAG" not in app_text:
    app_diag = """#if defined(__ANDROID__)
#include <android/log.h>
#define PACMAN_DIAG(...) __android_log_print(ANDROID_LOG_ERROR, "PacmanDiag", __VA_ARGS__)
#else
#define PACMAN_DIAG(...) ((void)0)
#endif
"""
    if app_anchor not in app_text:
        raise SystemExit("Could not find include anchor in src/ui/rex_app.cpp")
    app_text = app_text.replace(app_anchor, app_anchor + app_diag, 1)

old_init = """bool ReXApp::OnInitialize() {
  if (!SetupEnvironment())
    return false;
  if (!SetupPresentation())
    return false;

  auto paths = OnFinalizePaths(resolved_defaults_, MakeResumeCallback());
  if (!paths) {
    // Async: consumer will invoke resume when ready. OnInitialize returns
    // true so the event loop keeps pumping (wizard dialogs render).
    return true;
  }

  if (!ConstructRuntime(*paths))
    return false;
  LaunchModule();
  return true;
}"""
new_init = """bool ReXApp::OnInitialize() {
  PACMAN_DIAG("ReXApp: SetupEnvironment begin");
  if (!SetupEnvironment()) {
    PACMAN_DIAG("ReXApp: SetupEnvironment FAILED");
    return false;
  }
  PACMAN_DIAG("ReXApp: SetupEnvironment OK");
  PACMAN_DIAG("ReXApp: SetupPresentation begin");
  if (!SetupPresentation()) {
    PACMAN_DIAG("ReXApp: SetupPresentation FAILED");
    return false;
  }
  PACMAN_DIAG("ReXApp: SetupPresentation OK");

  auto paths = OnFinalizePaths(resolved_defaults_, MakeResumeCallback());
  PACMAN_DIAG("ReXApp: OnFinalizePaths returned %s", paths ? "paths" : "async/no paths");
  if (!paths) {
    // Async: consumer will invoke resume when ready. OnInitialize returns
    // true so the event loop keeps pumping (wizard dialogs render).
    return true;
  }

  PACMAN_DIAG("ReXApp: game_data_root='%s'", paths->game_data_root.string().c_str());
  PACMAN_DIAG("ReXApp: ConstructRuntime begin");
  if (!ConstructRuntime(*paths)) {
    PACMAN_DIAG("ReXApp: ConstructRuntime FAILED");
    return false;
  }
  PACMAN_DIAG("ReXApp: ConstructRuntime OK");
  LaunchModule();
  PACMAN_DIAG("ReXApp: LaunchModule queued");
  return true;
}"""
if old_init in app_text:
    app_text = app_text.replace(old_init, new_init, 1)
elif new_init not in app_text:
    raise SystemExit("Could not find ReXApp::OnInitialize block")

# Add explicit reason tags to known early returns in presentation/runtime setup.
pairs = [
("""      REXLOG_ERROR("Failed to load GPU plugin '{}'. See log for details.", config_.gpu_plugin);
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
      return false;""",
 """      REXLOG_ERROR("Failed to load GPU plugin '{}'. See log for details.", config_.gpu_plugin);
      PACMAN_DIAG("ReXApp: GPU plugin load failed: %s", config_.gpu_plugin.c_str());
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
      return false;"""),
("""      REXLOG_ERROR("Graphics presentation setup failed: {:08X}", status);
      return false;""",
 """      REXLOG_ERROR("Graphics presentation setup failed: {:08X}", status);
      PACMAN_DIAG("ReXApp: graphics SetupPresentation failed: %08X", status);
      return false;"""),
("""    REXLOG_ERROR("Failed to create window");
    return false;""",
 """    REXLOG_ERROR("Failed to create window");
    PACMAN_DIAG("ReXApp: Window::Create failed");
    return false;"""),
("""    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;""",
 """    REXLOG_ERROR("{}", msg);
    PACMAN_DIAG("ReXApp: %s", msg.c_str());
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    return false;"""),
("""    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    return false;""",
 """    REXLOG_ERROR("Runtime setup failed: {:08X}", status);
    PACMAN_DIAG("ReXApp: Runtime::Setup failed: %08X", status);
    return false;"""),
("""      REXLOG_ERROR("Failed to initialize rexcrt heap");
      return false;""",
 """      REXLOG_ERROR("Failed to initialize rexcrt heap");
      PACMAN_DIAG("ReXApp: InitHeap failed");
      return false;""")
]
for old_block,new_block in pairs:
    if old_block in app_text:
        app_text = app_text.replace(old_block,new_block,1)

app_path.write_text(app_text)
print("Added ReXApp initialization phase diagnostics to src/ui/rex_app.cpp.")
