#!/usr/bin/env python3
"""Install a SAF folder-copy launcher into the pinned SDL Android project.

Usage: python3 scripts/install-android-game-setup.py /path/to/SDL/android-project
The selected game tree is copied to app-private storage before SDL starts.
"""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

if len(sys.argv) != 2:
    raise SystemExit("usage: install-android-game-setup.py /path/to/SDL/android-project")

root = Path(sys.argv[1]).resolve()
app = root / "app"
manifest = app / "src/main/AndroidManifest.xml"
java = app / "src/main/java/org/libsdl/app"
if not manifest.is_file():
    raise SystemExit(f"SDL Android manifest not found: {manifest}")
java.mkdir(parents=True, exist_ok=True)

setup_activity = r'''package org.libsdl.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.database.Cursor;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class GameSetupActivity extends Activity {
    private static final int REQUEST_TREE = 4107;
    private static final String PREFS = "game_setup";
    private static final String PREF_URI = "tree_uri";
    private static final String EXTRA_GAME_ROOT = "org.libsdl.app.GAME_ROOT";
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private TextView status;
    private Button start;
    private Button choose;
    private ProgressBar progress;
    private Uri selectedTree;
    private File gameDir;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        gameDir = new File(getFilesDir(), "game");
        buildUi();
        String saved = getPreferences(MODE_PRIVATE).getString(PREF_URI, null);
        if (saved != null) {
            selectedTree = Uri.parse(saved);
            if (findEntryPoint(gameDir) != null) {
                status.setText("Pasta do jogo preparada. Os dados já estão no armazenamento privado.");
                start.setEnabled(true);
            } else {
                status.setText("Selecione novamente a pasta do jogo para preparar os arquivos.");
            }
        } else {
            status.setText("Selecione a pasta que contém Default.xex. O aplicativo copiará os arquivos para seu armazenamento privado.");
        }
    }

    private void buildUi() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int pad = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);
        status = new TextView(this);
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        layout.addView(status, new LinearLayout.LayoutParams(-1, -2));
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setIndeterminate(true);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(-1, -2);
        pp.topMargin = pad;
        layout.addView(progress, pp);
        choose = new Button(this);
        choose.setText("Selecionar pasta do jogo");
        choose.setOnClickListener(v -> chooseFolder());
        LinearLayout.LayoutParams bp = new LinearLayout.LayoutParams(-1, -2);
        bp.topMargin = pad;
        layout.addView(choose, bp);
        start = new Button(this);
        start.setText("Iniciar jogo");
        start.setEnabled(false);
        start.setOnClickListener(v -> launchGame());
        layout.addView(start, new LinearLayout.LayoutParams(-1, -2));
        setContentView(layout);
    }

    private void chooseFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                        Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_TREE);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != REQUEST_TREE || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(tree, flags & Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            status.setText("Não foi possível guardar a permissão da pasta: " + e.getMessage());
            return;
        }
        selectedTree = tree;
        prepareGame(tree);
    }

    private void prepareGame(Uri tree) {
        choose.setEnabled(false);
        start.setEnabled(false);
        progress.setVisibility(View.VISIBLE);
        status.setText("Verificando arquivos e espaço disponível...");
        worker.execute(() -> {
            try {
                File staging = new File(getFilesDir(), "game.new");
                deleteRecursively(staging);
                if (!staging.mkdirs() && !staging.isDirectory()) throw new Exception("Não foi possível criar a pasta temporária.");
                Uri rootUri = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
                copyDirectory(rootUri, staging);
                File entry = findEntryPoint(staging);
                if (entry == null) throw new Exception("Nenhum arquivo .xex válido foi encontrado na raiz da pasta selecionada.");
                File backup = new File(getFilesDir(), "game.old");
                deleteRecursively(backup);
                if (gameDir.exists() && !gameDir.renameTo(backup)) throw new Exception("Não foi possível preservar a cópia anterior.");
                if (!staging.renameTo(gameDir)) {
                    if (backup.exists()) backup.renameTo(gameDir);
                    throw new Exception("Não foi possível finalizar a cópia dos arquivos.");
                }
                deleteRecursively(backup);
                main.post(() -> {
                    progress.setVisibility(View.GONE);
                    choose.setEnabled(true);
                    start.setEnabled(true);
                    getPreferences(MODE_PRIVATE).edit().putString(PREF_URI, tree.toString()).apply();
                    status.setText("Arquivos preparados. Cópia concluída no armazenamento privado.");
                    Toast.makeText(this, "Pasta do jogo preparada.", Toast.LENGTH_LONG).show();
                });
            } catch (Exception e) {
                main.post(() -> {
                    progress.setVisibility(View.GONE);
                    choose.setEnabled(true);
                    start.setEnabled(false);
                    status.setText("Falha ao preparar os arquivos: " + e.getMessage());
                });
            }
        });
    }

    private void copyDirectory(Uri directoryUri, File destination) throws Exception {
        String documentId = DocumentsContract.getDocumentId(directoryUri);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(directoryUri, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                            DocumentsContract.Document.COLUMN_MIME_TYPE,
                            DocumentsContract.Document.COLUMN_SIZE};
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) throw new Exception("O provedor de arquivos não retornou a lista da pasta.");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = safeName(cursor.getString(1));
                String mime = cursor.getString(2);
                if (name.isEmpty() || ".".equals(name) || "..".equals(name)) continue;
                Uri child = DocumentsContract.buildDocumentUriUsingTree(directoryUri, id);
                File target = new File(destination, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!target.mkdirs() && !target.isDirectory()) throw new Exception("Não foi possível criar: " + name);
                    copyDirectory(child, target);
                } else {
                    copyFile(child, target);
                }
            }
        }
    }

    private String safeName(String value) {
        if (value == null) return "";
        String name = value.replace('/', '_').replace('\\', '_').trim();
        return name.length() > 240 ? name.substring(0, 240) : name;
    }

    private void copyFile(Uri source, File target) throws Exception {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new Exception("Falha ao criar diretório.");
        try (InputStream in = getContentResolver().openInputStream(source);
             FileOutputStream out = new FileOutputStream(target)) {
            if (in == null) throw new Exception("Não foi possível abrir um arquivo selecionado.");
            byte[] buffer = new byte[1024 * 1024];
            int count;
            while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            out.getFD().sync();
        }
    }

    private void launchGame() {
        File entry = findEntryPoint(gameDir);
        if (entry == null) {
            start.setEnabled(false);
            status.setText("Os arquivos do jogo não estão preparados. Selecione a pasta novamente.");
            return;
        }
        Intent intent = new Intent(this, GameSDLActivity.class);
        intent.putExtra(EXTRA_GAME_ROOT, gameDir.getAbsolutePath());
        startActivity(intent);
    }

    private static File findEntryPoint(File directory) {
        File[] files = directory.listFiles();
        if (files == null) return null;
        for (File file : files) {
            if (file.isFile() && file.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".xex") && file.length() >= 4) return file;
        }
        return null;
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteRecursively(child);
        }
        if (file.exists()) file.delete();
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        worker.shutdown();
    }
}
'''

game_activity = r'''package org.libsdl.app;

public final class GameSDLActivity extends SDLActivity {
    private static final String EXTRA_GAME_ROOT = "org.libsdl.app.GAME_ROOT";

    @Override protected String[] getArguments() {
        String root = getIntent().getStringExtra(EXTRA_GAME_ROOT);
        if (root == null || root.isEmpty()) return new String[0];
        return new String[] { "--game_data_root=" + root };
    }
}
'''

(java / "GameSetupActivity.java").write_text(setup_activity, encoding="utf-8")
(java / "GameSDLActivity.java").write_text(game_activity, encoding="utf-8")

tree = ET.parse(manifest)
mroot = tree.getroot()
ns = "{http://schemas.android.com/apk/res/android}"
app_node = mroot.find("application")
if app_node is None:
    raise SystemExit("AndroidManifest.xml has no application element")

# Convert the existing SDLActivity entry into the non-launcher game activity.
sdl = None
for activity in app_node.findall("activity"):
    name = activity.get(ns + "name", "")
    if name.endswith("SDLActivity") and not name.endswith("GameSDLActivity"):
        sdl = activity
        break
if sdl is None:
    raise SystemExit("Could not find the existing SDLActivity manifest entry")
sdl.set(ns + "name", "org.libsdl.app.GameSDLActivity")
sdl.set(ns + "exported", "false")
for filt in list(sdl.findall("intent-filter")):
    actions = [x.get(ns + "name") for x in filt.findall("action")]
    if "android.intent.action.MAIN" in actions:
        sdl.remove(filt)

# Avoid duplicate setup activity if the installer is run more than once.
for activity in list(app_node.findall("activity")):
    if activity.get(ns + "name") == "org.libsdl.app.GameSetupActivity":
        app_node.remove(activity)
setup = ET.Element("activity", {
    ns + "name": "org.libsdl.app.GameSetupActivity",
    ns + "exported": "true",
    ns + "label": "@string/app_name"
})
intent_filter = ET.SubElement(setup, "intent-filter")
ET.SubElement(intent_filter, "action", {ns + "name": "android.intent.action.MAIN"})
ET.SubElement(intent_filter, "category", {ns + "name": "android.intent.category.LAUNCHER"})
app_node.insert(0, setup)

ET.register_namespace("android", "http://schemas.android.com/apk/res/android")
tree.write(manifest, encoding="utf-8", xml_declaration=True)
print("[android-game-setup] SAF setup screen and SDL launch activity installed")
print("[android-game-setup] Selected folder is copied to app-private files/game before runtime launch")
