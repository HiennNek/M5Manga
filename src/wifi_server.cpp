#include "wifi_server.h"
#include "storage.h"
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <M5Unified.h>
#include <vector>

static WebServer server(80);
static bool running = false;
static String apSSID = "M5Manga-Reader";

// HTML template with modern JS for file operations
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>M5Manga File Browser</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; background: white; color: black; line-height: 1.6; }
        .container { 
            max-width: 1000px; 
            margin: 40px auto; 
            padding: 30px; 
            border: 3px solid black; 
            border-radius: 12px; 
            box-shadow: 8px 8px 0px 0px black;
        }
        h2 { font-size: 28px; text-transform: uppercase; letter-spacing: 2px; border-bottom: 3px solid black; padding-bottom: 15px; margin-top: 0; }
        .path { font-family: monospace; font-size: 16px; margin: 20px 0; padding: 8px 15px; background: #000; color: #fff; border-radius: 6px; display: inline-block; }
        .controls { margin-bottom: 30px; display: flex; gap: 15px; flex-wrap: wrap; }
        .btn { 
            padding: 12px 24px; 
            border: 2px solid black; 
            border-radius: 8px;
            background: white; 
            color: black; 
            cursor: pointer; 
            font-weight: bold; 
            text-transform: uppercase; 
            font-size: 13px;
            transition: transform 0.1s;
            box-shadow: 4px 4px 0px 0px black;
        }
        .btn:hover { transform: translate(-2px, -2px); box-shadow: 6px 6px 0px 0px black; }
        .btn:active { transform: translate(2px, 2px); box-shadow: 0px 0px 0px 0px black; }
        .btn:disabled { opacity: 0.3; cursor: not-allowed; box-shadow: none; transform: none; }
        
        table { width: 100%; border-collapse: separate; border-spacing: 0; margin-top: 20px; border: 2px solid black; border-radius: 8px; overflow: hidden; }
        th, td { padding: 15px; text-align: left; border-bottom: 2px solid black; }
        th { background: black; color: white; text-transform: uppercase; font-size: 14px; }
        tr:last-child td { border-bottom: none; }
        tr:nth-child(even) { background: #f9f9f9; }
        tr:hover { background: #eee; }
        
        input[type="file"] { display: none; }
        .checkbox-col { width: 50px; text-align: center; }
        input[type="checkbox"] { transform: scale(1.5); cursor: pointer; accent-color: black; }
        
        #status { 
            margin-top: 20px; 
            padding: 15px; 
            border: 2px solid black; 
            border-radius: 8px;
            font-weight: bold; 
            display: none; 
            text-transform: uppercase;
            box-shadow: 4px 4px 0px 0px black;
        }
        .progress { 
            width: 100%; 
            border: 2px solid black; 
            border-radius: 8px;
            height: 35px; 
            margin-top: 20px; 
            display: none; 
            overflow: hidden;
            background: white;
        }
        .progress-bar { 
            width: 0%; 
            height: 100%; 
            background: black; 
            color: white; 
            text-align: center; 
            line-height: 31px; 
            font-size: 14px; 
            font-weight: bold;
            transition: width 0.3s ease;
        }
        a { color: black; text-decoration: none; font-weight: bold; border-bottom: 2px solid transparent; }
        a:hover { border-bottom: 2px solid black; }
    </style>
</head>
<body>
    <div class="container">
        <h2>M5Manga File Browser</h2>
        <div class="path" id="currentPath">/</div>
        <div class="controls">
            <button class="btn" onclick="document.getElementById('fileInput').click()">Upload Files</button>
            <button class="btn" onclick="document.getElementById('folderInput').click()">Upload Folder</button>
            <button class="btn btn-red" id="btnDeleteSelected" onclick="deleteSelected()" disabled>Delete Selected</button>
            <button class="btn" onclick="syncTime()">Sync Time</button>
            <button class="btn" onclick="goBack()">Back</button>
            <input type="file" id="fileInput" multiple onchange="uploadFiles(this.files)">
            <input type="file" id="folderInput" webkitdirectory mozdirectory msdirectory odirectory directory onchange="uploadFiles(this.files)">
        </div>
        
        <div id="status"></div>
        <div class="progress"><div id="progressBar" class="progress-bar">0%</div></div>

        <table id="fileTable">
            <thead>
                <tr>
                    <th class="checkbox-col"><input type="checkbox" id="selectAll" onclick="toggleSelectAll(this)"></th>
                    <th>Name</th>
                    <th>Size</th>
                    <th>Actions</th>
                </tr>
            </thead>
            <tbody id="fileList"></tbody>
        </table>
    </div>

    <script>
        let currentDir = "/";
        
        async function loadFiles() {
            try {
                const response = await fetch(`/list?dir=${encodeURIComponent(currentDir)}`);
                const files = await response.json();
                const list = document.getElementById('fileList');
                list.innerHTML = "";
                document.getElementById('currentPath').innerText = currentDir;
                document.getElementById('selectAll').checked = false;
                updateDeleteButton();

                files.sort((a,b) => (a.type === b.type) ? a.name.localeCompare(b.name) : (a.type === "dir" ? -1 : 1));

                files.forEach(file => {
                    const tr = document.createElement('tr');
                    const isDir = file.type === "dir";
                    tr.innerHTML = `
                        <td class="checkbox-col"><input type="checkbox" class="file-check" data-path="${file.name}" onchange="updateDeleteButton()"></td>
                        <td>${isDir ? '<b>[DIR]</b>' : '[FILE]'} <a href="#" onclick="${isDir ? `changeDir('${file.name}')` : `downloadFile('${file.name}')`}">${file.name}</a></td>
                        <td>${isDir ? '-' : formatBytes(file.size)}</td>
                        <td>
                            <button class="btn" onclick="renameFile('${file.name}')">Rename</button>
                            <button class="btn btn-red" onclick="deleteFile('${file.name}')">Delete</button>
                        </td>
                    `;
                    list.appendChild(tr);
                });
            } catch (e) {
                showStatus("Error: Could not load file list", "black");
            }
        }

        function changeDir(name) {
            if (currentDir === "/") currentDir = "/" + name;
            else currentDir = currentDir + "/" + name;
            loadFiles();
        }

        function goBack() {
            if (currentDir === "/") return;
            const parts = currentDir.split("/");
            parts.pop();
            currentDir = parts.join("/") || "/";
            loadFiles();
        }

        function formatBytes(bytes) {
            if (bytes === 0) return '0 Bytes';
            const k = 1024;
            const sizes = ['Bytes', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        }

        function toggleSelectAll(master) {
            document.querySelectorAll('.file-check').forEach(cb => cb.checked = master.checked);
            updateDeleteButton();
        }

        function updateDeleteButton() {
            const checked = document.querySelectorAll('.file-check:checked').length;
            document.getElementById('btnDeleteSelected').disabled = checked === 0;
        }

        async function deleteFile(name) {
            if (!confirm(`Delete ${name}?`)) return;
            const fullPath = (currentDir === "/" ? "" : currentDir) + "/" + name;
            await performDelete([fullPath]);
        }

        async function deleteSelected() {
            const checks = document.querySelectorAll('.file-check:checked');
            if (checks.length === 0) return;
            if (!confirm(`Delete ${checks.length} selected items?`)) return;
            const paths = Array.from(checks).map(cb => (currentDir === "/" ? "" : currentDir) + "/" + cb.getAttribute('data-path'));
            await performDelete(paths);
        }

        async function performDelete(paths) {
            showStatus("Action: Deleting...", "black");
            try {
                const response = await fetch('/delete', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ paths })
                });
                if (response.ok) {
                    showStatus("Status: Deleted successfully", "black");
                    loadFiles();
                } else {
                    showStatus("Error: Delete failed", "black");
                }
            } catch (e) { showStatus("Error: Communication failure during delete", "black"); }
        }

        function renameFile(name) {
            const newName = prompt("New name:", name);
            if (!newName || newName === name) return;
            const oldPath = (currentDir === "/" ? "" : currentDir) + "/" + name;
            const newPath = (currentDir === "/" ? "" : currentDir) + "/" + newName;
            
            fetch(`/rename?old=${encodeURIComponent(oldPath)}&new=${encodeURIComponent(newPath)}`)
                .then(r => {
                    if (r.ok) loadFiles();
                    else alert("Rename failed");
                });
        }

        async function uploadFiles(files) {
            if (files.length === 0) return;
            
            const progressBar = document.getElementById('progressBar');
            const progressContainer = document.querySelector('.progress');
            progressContainer.style.display = 'block';
            showStatus(`Action: Uploading ${files.length} items...`, "black");

            for (let i = 0; i < files.length; i++) {
                const file = files[i];
                const relativePath = file.webkitRelativePath || file.name;
                const fullPath = (currentDir === "/" ? "" : currentDir) + "/" + relativePath;
                
                progressBar.innerText = `[${Math.round(((i + 1) / files.length) * 100)}%] UPLOADING: ${file.name}`;
                const formData = new FormData();
                formData.append('file', file, fullPath);

                try {
                    const response = await fetch('/upload', {
                        method: 'POST',
                        body: formData
                    });
                    if (!response.ok) throw new Error("Upload failed");
                    
                    const percent = Math.round(((i + 1) / files.length) * 100);
                    progressBar.style.width = percent + "%";
                } catch (e) {
                    showStatus(`Error: Failed at ${file.name}`, "black");
                    break;
                }
            }
            
            showStatus("Status: Upload complete", "black");
            setTimeout(() => { progressContainer.style.display = 'none'; }, 2000);
            loadFiles();
        }

        function showStatus(msg, color) {
            const s = document.getElementById('status');
            s.innerText = msg;
            s.style.display = 'block';
            s.style.background = "black";
            s.style.color = "white";
        }

        async function syncTime() {
            const now = new Date();
            const payload = {
                year: now.getFullYear(),
                month: now.getMonth() + 1,
                day: now.getDate(),
                hours: now.getHours(),
                minutes: now.getMinutes(),
                seconds: now.getSeconds()
            };
            showStatus("Action: Syncing time...", "black");
            try {
                const response = await fetch('/sync_time', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                if (response.ok) {
                    showStatus("Status: Time synchronized", "black");
                } else {
                    showStatus("Error: Sync failed", "black");
                }
            } catch (e) { showStatus("Error: Sync communication failure", "black"); }
        }

        loadFiles();
    </script>
</body>
</html>
)rawliteral";

static void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

static void handleList()
{
  String path = server.arg("dir");
  if (path == "")
    path = "/";

  File root = SD.open(path);
  if (!root || !root.isDirectory())
  {
    server.send(404, "text/plain", "Not Found");
    return;
  }

  String output = "[";
  output.reserve(1024); // Start with a reasonable size
  File file = root.openNextFile();
  while (file)
  {
    if (output.length() > 1)
      output += ",";
    output += "{\"name\":\"";
    output += file.name();
    output += "\",\"type\":\"";
    output += (file.isDirectory() ? "dir" : "file");
    output += "\",\"size\":";
    output += String(file.size());
    output += "}";

    file = root.openNextFile();
  }
  output += "]";
  server.send(200, "application/json", output);
}

static void handleDelete()
{
  if (server.hasArg("plain") == false)
  {
    server.send(400, "text/plain", "Body not received");
    return;
  }
  String body = server.arg("plain");
  // Simple JSON parse for {"paths":["/path1", "/path2"]}
  int start = body.indexOf("[");
  int end = body.lastIndexOf("]");
  if (start == -1 || end == -1)
  {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  String pathsStr = body.substring(start + 1, end);

  while (pathsStr.length() > 0)
  {
    int nextQuote = pathsStr.indexOf("\"");
    if (nextQuote == -1)
      break;
    pathsStr = pathsStr.substring(nextQuote + 1);
    int endQuote = pathsStr.indexOf("\"");
    if (endQuote == -1)
      break;
    String path = pathsStr.substring(0, endQuote);

    Serial.printf("Deleting: %s\n", path.c_str());
    if (SD.exists(path))
    {
      File f = SD.open(path);
      if (f.isDirectory())
      {
        f.close();
        // Invalidate cache if this is a manga folder
        if (path.startsWith(MANGA_ROOT))
          invalidatePageCountCache(path);
        SD.rmdir(path.c_str());
      }
      else
      {
        f.close();
        SD.remove(path.c_str());
        // Invalidate cache for the parent manga folder
        if (path.startsWith(MANGA_ROOT))
        {
          int lastSlash = path.lastIndexOf('/');
          if (lastSlash > 0)
            invalidatePageCountCache(path.substring(0, lastSlash));
        }
      }
    }

    pathsStr = pathsStr.substring(endQuote + 1);
    int comma = pathsStr.indexOf(",");
    if (comma != -1)
      pathsStr = pathsStr.substring(comma + 1);
    else
      break;
  }
  server.send(200, "text/plain", "OK");
}

static void handleRename()
{
  String oldPath = server.arg("old");
  String newPath = server.arg("new");
  if (oldPath != "" && newPath != "")
  {
    if (SD.rename(oldPath, newPath))
    {
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(500, "text/plain", "Rename failed");
}

static void handleUpload()
{
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START)
  {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;

    // Ensure directories exist
    int slashIdx = filename.lastIndexOf('/');
    if (slashIdx > 0)
    {
      String dir = filename.substring(0, slashIdx);
      if (!SD.exists(dir))
      {
        // Create recursive directories
        String current = "";
        String remaining = dir;
        if (remaining.startsWith("/"))
          remaining = remaining.substring(1);
        while (remaining.length() > 0)
        {
          int nextSlash = remaining.indexOf('/');
          String part;
          if (nextSlash != -1)
          {
            part = remaining.substring(0, nextSlash);
            remaining = remaining.substring(nextSlash + 1);
          }
          else
          {
            part = remaining;
            remaining = "";
          }
          current += "/" + part;
          if (!SD.exists(current))
            SD.mkdir(current);
        }
      }
    }

    Serial.printf("Upload Start: %s\n", filename.c_str());
    File file = SD.open(filename, FILE_WRITE);
    if (!file)
    {
      Serial.println("Failed to open file for writing");
    }
    file.close(); // Will re-open in write mode during UPLOAD_FILE_WRITE
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    File file = SD.open(filename, FILE_APPEND);
    if (file)
    {
      file.write(upload.buf, upload.currentSize);
      file.close();
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    // Invalidate page count cache if file was uploaded under /manga/
    String filename = upload.filename;
    if (!filename.startsWith("/"))
      filename = "/" + filename;
    if (filename.startsWith(MANGA_ROOT))
    {
      int lastSlash = filename.lastIndexOf('/');
      if (lastSlash > 0)
        invalidatePageCountCache(filename.substring(0, lastSlash));
    }
    server.send(200, "text/plain", "OK");
  }
}

static void handleSyncTime()
{
  if (server.hasArg("plain") == false)
  {
    server.send(400, "text/plain", "Body not received");
    return;
  }
  String body = server.arg("plain");
  auto getVal = [&](String key) -> int {
    int idx = body.indexOf("\"" + key + "\":");
    if (idx == -1)
      return -1;
    int start = body.indexOf(":", idx) + 1;
    int end = body.indexOf(",", start);
    if (end == -1)
      end = body.indexOf("}", start);
    return (int)body.substring(start, end).toInt();
  };

  m5::rtc_datetime_t dt;
  dt.date.year = getVal("year");
  dt.date.month = getVal("month");
  dt.date.date = getVal("day");
  dt.time.hours = getVal("hours");
  dt.time.minutes = getVal("minutes");
  dt.time.seconds = getVal("seconds");

  if (dt.date.year > 2000)
  {
    M5.Rtc.setDateTime(dt);
    server.send(200, "text/plain", "OK");
  }
  else
  {
    server.send(400, "text/plain", "Invalid Date");
  }
}

void startWifiServer()
{
  if (running)
    return;
  WiFi.softAP(apSSID.c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/list", HTTP_GET, handleList);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/rename", HTTP_GET, handleRename);
  server.on("/upload", HTTP_POST, []()
            { server.send(200); }, handleUpload);
  server.on("/sync_time", HTTP_POST, handleSyncTime);

  server.begin();
  running = true;
  Serial.println("WiFi Server Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopWifiServer()
{
  if (!running)
    return;
  server.stop();
  WiFi.softAPdisconnect(true);
  running = false;
  Serial.println("WiFi Server Stopped");
}

void updateWifiServer()
{
  if (running)
    server.handleClient();
}

bool isWifiServerRunning() { return running; }
String getWifiIP() { return WiFi.softAPIP().toString(); }
String getWifiSSID() { return apSSID; }
