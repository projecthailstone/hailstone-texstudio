-- Hailstone Texture Studio's window (README.md): not a game window. The studio loads only this
-- script of the modern pack (LuaUi::set_entry_scripts), so it has the pack's look (lib/pregame.lua's cards over the
-- client's pictures, lib/theme.lua's palette) and none of its game windows; the client never loads it (it is not at
-- the pack's top). Everything it shows and does goes through the `studio` table (src/studio_lua.cpp).
if not studio then return end

local P = dofile("lib/pregame.lua")
local T = P.T
local C = T.color

local function rgba(c) return c[1], c[2], c[3], c[4] or 1 end

-- ---------------------------------------------------------------- formatting
local function thousands(n)
  local s = string.format("%d", math.floor((n or 0) + 0.5))
  local out = s:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  return (out:gsub("^,", ""))
end

local function bytes(n)
  n = n or 0
  if n >= 1e9 then return string.format("%.1f GB", n / 1e9) end
  if n >= 1e6 then return string.format("%.0f MB", n / 1e6) end
  return string.format("%.0f KB", n / 1e3)
end

local function duration(secs)
  secs = math.floor((secs or 0) + 0.5)
  if secs < 60 then return "under a minute" end
  local h, m = math.floor(secs / 3600), math.floor(secs % 3600 / 60)
  if h == 0 then return string.format("%d min", m) end
  if h < 10 then return string.format("%d h %02d min", h, m) end
  return string.format("%d h", h)
end

-- ---------------------------------------------------------------- state kept between frames
local pathText = nil      -- the asset folder field while it is typed in
local nameText = nil      -- the pack name field while it is typed in
local search = ""         -- the zone list's filter
local findText = ""       -- the preview's search
local startError = nil    -- why Build did not start
local split = 0.5         -- the preview's wipe between the original and the HD picture
local tiled = false       -- the preview's HD picture as 3 x 3 tiles
local mapView = "color"   -- the preview's right side: color, normal or rough
local groupsRev, groups = -1, {}
local firstPreview = false
local packUsed = nil      -- the pack just chosen for Hailstone (a message)

local LEFT_W, RIGHT_W = 540, 560

local function field(id, hint, text, w)
  ui.SetNextItemWidth(w or -1)
  local t, enter = ui.InputTextWithHint(id, hint, text, 512, ui.InputTextFlags.EnterReturnsTrue)
  return t, enter
end

local function pill(text, col, s)
  local x, y = ui.GetCursorScreenPos()
  local w, h = P.Pill(x, y, text, col, s)
  ui.Dummy(w, h)
end

-- ---------------------------------------------------------------- sections
local function gameFiles(st, s)
  P.Heading("Game files")
  local text = pathText or st.assetsPath
  local bw = P.ButtonsWidth({ "Browse..." }, s)
  local t, enter = field("##assets", "The folder of your EverQuest files", text, ui.GetContentRegionAvail() - bw - 8 * s)
  if ui.IsItemActive() then pathText = t elseif pathText then
    if pathText ~= st.assetsPath then studio.Set("assetsPath", pathText) end
    pathText = nil
  end
  if enter then
    studio.Set("assetsPath", t)
    pathText = nil
  end
  P.Tip("Hailstone only reads this folder. The pack is written to Hailstone's own textures folder.")
  ui.SameLine()
  ui.BeginDisabled(st.browsing)
  if ui.Button("Browse...") then studio.Browse() end
  ui.EndDisabled()
  if #st.recent > 1 then
    ui.SetNextItemWidth(-1)
    if ui.BeginCombo("##recent", "Folders Hailstone has used") then
      for _, r in ipairs(st.recent) do
        if ui.Selectable(r, r == st.assetsPath) then studio.Set("assetsPath", r) end
      end
      ui.EndCombo()
    end
  end
  if st.serverFolders > 0 then
    local v, changed = ui.Checkbox(string.format("Include my servers' own files (%d)", st.serverFolders), st.includeServers)
    if changed then studio.Set("includeServers", v) end
    P.Tip("Textures a server ships in its own files (servers/<server>/assets) are made too.")
  end
  local scan = st.scan
  if scan.state == "scanning" then
    P.Status({ text = string.format("Reading the game's archives: %d of %d", scan.done, scan.total) }, true, s)
  elseif scan.state == "failed" then
    P.Status({ text = st.assetsProblem ~= "" and st.assetsProblem or "The folder could not be read.", error = true }, false, s)
  else
    P.Status({ text = scan.message }, false, s)
    if st.assetsProblem ~= "" then P.Faint(st.assetsProblem) end
  end
end

local function quality(st, s)
  P.Heading("Quality")
  local n = #st.presets
  local w = (ui.GetContentRegionAvail() - 8 * s * (n - 1)) / n
  local current
  for i, p in ipairs(st.presets) do
    if i > 1 then ui.SameLine() end
    local on = st.preset == p.id
    if on then current = p end
    local A = C.accent
    if on then
      ui.PushStyleColor(ui.Col.Button, A[1], A[2], A[3], 0.85)
      ui.PushStyleColor(ui.Col.Text, rgba(C.onAccent))
    end
    if ui.Button(p.title .. "##preset", w, 0) then studio.Set("preset", p.id) end
    if on then ui.PopStyleColor(2) end
    P.Tip(p.note)
  end
  if current then
    ui.PushTextWrapPos(0)
    P.Muted(current.note)
    ui.PopTextWrapPos()
  end
  if st.preset == "custom" then
    local title = st.colorModel
    for _, m in ipairs(st.colorModels) do if m.id == st.colorModel then title = m.title end end
    ui.SetNextItemWidth(260 * s)
    if ui.BeginCombo("Upscaler", title) then
      for _, m in ipairs(st.colorModels) do
        if ui.Selectable(m.title .. "##" .. m.id, m.id == st.colorModel) then studio.Set("colorModel", m.id) end
        local tip = m.note
        if m.license ~= "" then tip = tip .. "\n" .. m.license end
        if m.user then tip = tip .. "\n(one of yours)" end
        P.Tip(tip)
      end
      ui.EndCombo()
    end
    ui.SameLine()
    if ui.SmallButton("Add models...") then studio.OpenFolder("models") end
    P.Tip("Put ncnn models (.param and .bin) in this folder; the studio's README says how to make one.")
    ui.SetNextItemWidth(260 * s)
    local scale, changed = ui.SliderInt("Size", st.scale, 1, 4, st.scale .. "x the game's")
    if changed then studio.Set("scale", scale) end
    ui.BeginDisabled(not st.mapsAvailable)
    local maps, mc = ui.Checkbox("Normal and roughness maps", st.maps)
    if mc then studio.Set("maps", maps) end
    ui.EndDisabled()
    P.Tip("The modern lighting shades the surfaces with them: bumps and highlights.")
  end
  -- the device
  local dev = st.devices[st.device]
  local function label(d)
    if not d then return "none" end
    if d.type == "cpu" and d.name == "CPU" then return "CPU (slow)" end
    local mem = d.memoryMb > 0 and string.format(", %.1f GB", d.memoryMb / 1024) or ""
    return string.format("%s (%s%s)", d.name, d.type, mem)
  end
  ui.SetNextItemWidth(-1)
  if ui.BeginCombo("##device", "Runs on: " .. label(dev)) then
    for i, d in ipairs(st.devices) do
      if ui.Selectable(label(d) .. "##dev" .. i, i == st.device) then studio.Set("device", i) end
    end
    ui.EndCombo()
  end
  if not st.gpuFound then
    ui.PushTextWrapPos(0)
    P.Faint("No GPU answered through Vulkan, so the CPU does the work: dozens of times slower. An up-to-date graphics driver may help.")
    ui.PopTextWrapPos()
  end
  local w = st.worker
  if w.state == "failed" or (w.state == "none" and w.error ~= "") then
    P.Status({ text = w.error, error = true }, false, s)
  elseif w.state == "loading" then
    P.Status({ text = "Loading the models..." }, true, s)
  elseif st.measuring then
    P.Status({ text = "Timing this device..." }, true, s)
  end
end

local function scope(st, s)
  P.Heading("What to build")
  local v, c = ui.Checkbox("Every zone and everything they share", st.buildAll)
  if c then studio.Set("buildAll", v) end
  if not st.buildAll then
    local rev, list = studio.Groups()
    if rev ~= groupsRev then groupsRev, groups = rev, list end
    local chosen = 0
    for _, g in ipairs(groups) do if g.selected then chosen = chosen + 1 end end
    local cw = P.ButtonsWidth({ "None" }, s)
    search = field("##zonesearch", "Find a zone", search, ui.GetContentRegionAvail() - cw - 8 * s)
    ui.SameLine()
    if ui.Button("None") then studio.Set("noGroups") end
    ui.BeginChild("##zones", 0, 170 * s, true)
    local q = search:lower()
    for _, g in ipairs(groups) do
      if q == "" or g.title:lower():find(q, 1, true) or g.key:find(q, 1, true) then
        local on, ch = ui.Checkbox(string.format("%s##g%s", g.title, g.key), g.selected)
        if ch then studio.Set("group:" .. g.key, on) end
        ui.SameLine()
        P.Faint(thousands(g.textures))
      end
    end
    ui.EndChild()
    ui.PushTextWrapPos(0)
    P.Faint(string.format("%d chosen. The shared group holds the characters, equipment and spells every zone uses.", chosen))
    ui.PopTextWrapPos()
  end
end

local function packSection(st, s)
  P.Heading("Pack")
  local text = nameText or st.packName
  P.Caption("Its name: a folder in Hailstone's textures folder")
  ui.SetNextItemWidth(220 * s)
  local t, enter = ui.InputTextWithHint("##pack", "hd", text, 64, ui.InputTextFlags.EnterReturnsTrue)
  if ui.IsItemActive() then nameText = t elseif nameText then
    studio.Set("packName", nameText)
    nameText = nil
  end
  if enter then studio.Set("packName", t) nameText = nil end
  P.Tip("Letters, digits, '-', '_' and '.'.")
  ui.SameLine()
  if ui.SmallButton("Open folder") then studio.OpenFolder("pack") end
  if st.packMismatch then
    local i = st.packInfo
    ui.PushTextWrapPos(0)
    ui.TextColored(C.danger[1], C.danger[2], C.danger[3], 1, string.format(
      "This pack was built with other settings (%dx%s, %s). Choose another name, or start it over.",
      i.scale, i.maps and " with maps" or "", i.colorModel))
    ui.PopTextWrapPos()
    if ui.Button("Use " .. st.suggestedName) then studio.Set("packName", st.suggestedName) end
    ui.SameLine()
    if P.Danger("Start it over...", 0, 0, true) then ui.OpenPopup("##clear") end
  end
  if P.Confirm("##clear", "Delete the textures this pack holds?", "Its folder in Hailstone's textures folder is emptied. " ..
      "Your game files are not touched.", "Delete", s) then
    studio.ClearPack()
  end
  -- the estimate
  local e = st.estimate
  if st.scan.state == "ready" and e.textures > 0 then
    local line
    if e.todo == 0 then
      line = string.format("All %s textures are in the pack.", thousands(e.textures))
    else
      line = string.format("%s textures to make", thousands(e.todo))
      if e.complete > 0 then line = line .. string.format(" (%s already made)", thousands(e.complete)) end
      if e.seconds > 0 then line = line .. ", about " .. duration(e.seconds) .. " on this device" end
    end
    ui.PushTextWrapPos(0)
    P.Muted(line)
    P.Faint(string.format("The pack takes about %s on disk, and as much video memory as Hailstone shows of it.", bytes(e.bytes)))
    if e.freeBytes >= 0 and e.todo > 0 then
      if e.freeBytes < e.todoBytes + 1e9 then
        ui.TextColored(C.danger[1], C.danger[2], C.danger[3], 1, string.format(
          "Only %s is free there: not enough. Choose fewer zones or a smaller size.", bytes(e.freeBytes)))
      else
        P.Faint(string.format("%s free there.", bytes(e.freeBytes)))
      end
    end
    ui.PopTextWrapPos()
  end
end

local function buildSection(st, s)
  local j = st.job
  local running = j and (j.state == "running" or j.state == "paused" or j.state == "stopping")
  local _, lh = ui.CalcTextSize("Ag")
  local bh = lh + 14 * s
  ui.Dummy(0, 4 * s)
  if not running then
    local e = st.estimate
    local room = e.freeBytes < 0 or e.freeBytes >= e.todoBytes + 1e9
    local ready = st.scan.state == "ready" and st.worker.state ~= "failed" and not st.packMismatch and e.todo > 0 and room
    if P.Primary(st.estimate.complete > 0 and "Continue the build" or "Build the pack", -1, bh, ready) then
      local ok, why = studio.Start()
      startError = (not ok) and why or nil
    end
    if startError then P.Status({ text = startError, error = true }, false, s) end
  else
    local w = (ui.GetContentRegionAvail() - 8 * s) / 2
    if j.state == "paused" then
      if P.Primary("Resume", w, bh, true) then studio.Resume() end
    else
      ui.BeginDisabled(j.state == "stopping")
      if ui.Button("Pause", w, bh) then studio.Pause() end
      ui.EndDisabled()
    end
    ui.SameLine()
    if P.Danger("Stop", w, bh, j.state ~= "stopping") then studio.Stop() end
  end
  local li, lc = ui.Checkbox("Low impact", st.lowImpact)
  if lc then studio.Set("lowImpact", li) end
  P.Tip("Half the GPU's time and a quarter of the CPU's, so you can play or work beside it. About twice as long.")
  if j then
    local x0, y0 = ui.GetCursorScreenPos()
    local x1 = x0 + ui.GetContentRegionAvail()
    local col = j.state == "failed" and C.danger or C.accent
    local text = string.format("%s of %s", thousands(j.done), thousands(j.total))
    T.Bar("studio.progress", x0, y0, x1, y0 + 20 * s, s, j.fraction or 0, col, { text = text })
    ui.Dummy(0, 22 * s)
    local line
    if j.state == "running" then
      line = "Building"
      if j.eta > 0 then line = line .. ", about " .. duration(j.eta) .. " left" end
      if j.current ~= "" then line = line .. " - " .. j.current end
    elseif j.state == "paused" then line = "Paused. Closing the studio keeps what is made; the build goes on from there next time."
    elseif j.state == "stopping" then line = "Stopping..."
    elseif j.state == "done" then line = string.format("Done in %s: %s files, %s.", duration(j.seconds), thousands(j.filesWritten), bytes(j.bytesWritten))
    elseif j.state == "cancelled" then line = "Stopped. What was made is kept: Continue the build goes on from there."
    elseif j.state == "failed" then line = "The build stopped: " .. j.fatal end
    ui.PushTextWrapPos(0)
    P.Muted(line)
    ui.PopTextWrapPos()
    if j.failures > 0 then
      if ui.CollapsingHeader(string.format("%d textures could not be made##errs", j.failures)) then
        ui.BeginChild("##errors", 0, 90 * s, true)
        for _, e in ipairs(j.errors) do P.Faint(e) end
        ui.EndChild()
        if ui.SmallButton("Open the log folder") then studio.OpenFolder("logs") end
      end
    end
    if j.state == "done" or j.state == "cancelled" then
      ui.Dummy(0, 2 * s)
      local active = st.activePack
      if active == st.packName then
        pill("Hailstone uses this pack", C.good or C.accent, s)
      elseif P.Primary("Use this pack in Hailstone", -1, bh, true) then
        if studio.UsePack(st.packName) then packUsed = st.packName end
      end
    end
  end
  if packUsed then P.Faint("Hailstone uses " .. packUsed .. " from its next start.") end
end

-- ---------------------------------------------------------------- the preview
local function previewImages(st, s, width)
  local p = st.preview
  local side = width
  if p.state == "none" then
    ui.Dummy(side, side * 0.6)
    return
  end
  local aspect = (p.height > 0 and p.width > 0) and p.height / p.width or 1
  local h = math.min(side * aspect, 300 * s)
  local w = h / aspect
  if p.state == "working" then
    P.Status({ text = "Making " .. p.name .. "..." }, true, s)
    ui.Dummy(w, h - 20 * s)
    return
  end
  if p.state == "failed" then
    P.Status({ text = p.name .. ": " .. p.error, error = true }, false, s)
    return
  end
  local src = studio.Picture(p.source)
  local rightPath = mapView == "normal" and p.normal or mapView == "rough" and p.rough or p.color
  if rightPath == "" then rightPath = p.color end
  local hd = studio.Picture(rightPath)
  local x0, y0 = ui.GetCursorScreenPos()
  ui.InvisibleButton("##wipe", w, h)
  if ui.IsItemActive() then
    local mx = ui.GetMousePos()
    split = math.max(0, math.min(1, (mx - x0) / w))
  end
  local x1, y1 = x0 + w, y0 + h
  local xs = x0 + w * split
  if tiled then
    -- 3 x 3: seams between the tiles show where a tiling texture's edges do not meet
    local tw, th = w / 3, h / 3
    for ty = 0, 2 do
      for tx = 0, 2 do
        if hd then ui.DrawImage(hd, x0 + tx * tw, y0 + ty * th, x0 + (tx + 1) * tw, y0 + (ty + 1) * th) end
      end
    end
  else
    if src then ui.DrawImage(src, x0, y0, xs, y1, 0, 0, split, 1) end
    if hd then ui.DrawImage(hd, xs, y0, x1, y1, split, 0, 1, 1) end
    ui.DrawLine(xs, y0, xs, y1, C.text[1], C.text[2], C.text[3], 0.9, 2 * s)
    ui.DrawCircleFilled(xs, (y0 + y1) / 2, 6 * s, C.accent[1], C.accent[2], C.accent[3], 1)
    T.Text(x0 + 6 * s, y0 + 4 * s, "Original", C.text, T.font.small)
    local label = mapView == "normal" and "Normal map" or mapView == "rough" and "Roughness" or "HD"
    local lw = T.TextSize(label, T.font.small)
    T.Text(x1 - lw - 6 * s, y0 + 4 * s, label, C.text, T.font.small)
  end
  ui.DrawRect(x0, y0, x1, y1, C.frameBorder[1], C.frameBorder[2], C.frameBorder[3], 1, 0, 1)
end

local function previewCard(st, s)
  P.Heading("Preview")
  P.Faint("One of your textures, made with these settings. Drag across it to compare.")
  local scanReady = st.scan.state == "ready" and st.worker.state == "ready"
  if scanReady and not firstPreview then
    firstPreview = true
    studio.Preview("")
  end
  ui.BeginDisabled(not scanReady)
  if ui.Button("Another texture") then studio.Preview("") end
  ui.SameLine()
  local fw = P.ButtonsWidth({ "Find" }, s)
  local t, enter = field("##find", "A texture's name, e.g. kbark", findText, ui.GetContentRegionAvail() - fw - 8 * s)
  findText = t
  ui.SameLine()
  if ui.Button("Find") or enter then
    if not studio.Preview(findText) then startError = nil end
  end
  ui.EndDisabled()
  local p = st.preview
  -- what the right side shows
  for i, v in ipairs({ { "color", "HD" }, { "normal", "Normal" }, { "rough", "Roughness" } }) do
    if i > 1 then ui.SameLine() end
    local disabled = (v[1] == "normal" and p.normal == "") or (v[1] == "rough" and p.rough == "")
    ui.BeginDisabled(disabled)
    if ui.Selectable(v[2] .. "##view", mapView == v[1], 0, 70 * s, 0) then mapView = v[1] end
    ui.EndDisabled()
  end
  ui.SameLine()
  local tv, tc = ui.Checkbox("Tile it", tiled)
  if tc then tiled = tv end
  P.Tip("The HD texture 3 x 3, to see whether its edges meet.")
  ui.Dummy(0, 2 * s)
  previewImages(st, s, ui.GetContentRegionAvail())
  if p.state == "ready" then
    local info = string.format("%s: %d x %d -> %d x %d, %.1f s", p.name, p.width, p.height, p.width * p.scale,
      p.height * p.scale, p.seconds)
    P.Muted(info)
    P.Faint(p.archives)
    if p.data then P.Faint("Data, not a picture: it is only scaled, and gets no maps.") end
  end
end

local function packsCard(st, s)
  P.Heading("Your packs")
  local active = st.activePack
  local function row(name, title, detail, inUse)
    local _, lh = ui.CalcTextSize("Ag")
    local clicked, _, x0, y0, x1, y1 = P.Row("##pk" .. name, inUse, lh * 2 + 10 * s, s)
    T.Text(x0 + 12 * s, y0 + 4 * s, title, C.text)
    T.Text(x0 + 12 * s, y0 + 6 * s + lh, detail, C.muted, T.font.small)
    if inUse then
      local tw = T.TextSize("In use", T.font.tiny)
      P.Pill(x1 - tw - 24 * s, y0 + 6 * s, "In use", C.accent, s)
    end
    if clicked and not inUse then
      if studio.UsePack(name) then packUsed = name end
    end
    P.Tip(inUse and "Hailstone uses this one." or "Click to have Hailstone use this one.")
  end
  for _, p in ipairs(st.packs) do
    local detail
    if p.textures then
      detail = string.format("%s textures, %s, %dx%s%s", thousands(p.textures), bytes(p.bytes), p.scale or 2,
        p.maps and " with maps" or "", p.built ~= "" and (", " .. p.built) or "")
    else
      detail = "a folder of replacement textures"
    end
    row(p.name, p.name, detail, active == p.name or (active == "" and p.name == "default"))
  end
  row("off", "Off", "The game's own textures", active == "off")
end

-- ---------------------------------------------------------------- the window
ui.window("TextureStudio", function()
  local st = studio.State()
  local s = P.Scale()
  local io = ui.GetIO()
  local gap, margin = 18 * s, 16 * s
  local lw, rw = LEFT_W * s, RIGHT_W * s
  local side = io.width >= lw + rw + gap + 2 * margin
  local lx, rx, top
  if side then
    lx = math.floor((io.width - lw - rw - gap) / 2)
    rx = lx + lw + gap
    top = margin
  else
    lx, rx, top = math.floor((io.width - lw) / 2), nil, margin
  end
  if P.Begin("studio.left", lx, top, lw, s) then
    P.Title("Texture Studio", "HD textures made from your own game files, on your computer. " .. st.version, s)
    gameFiles(st, s)
    quality(st, s)
    scope(st, s)
    if not side then
      packSection(st, s)
      buildSection(st, s)
      previewCard(st, s)
      packsCard(st, s)
    end
  end
  P.End()
  if side then
    -- the pack and its build first, so the main action is in sight whatever the left card holds
    if P.Begin("studio.right", rx, top, rw, s) then
      packSection(st, s)
      buildSection(st, s)
      previewCard(st, s)
      packsCard(st, s)
    end
    P.End()
  end
end)
