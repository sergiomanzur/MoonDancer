local frame = 0
local function log(msg) emu.print_info(msg) end
emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= 900 then return end
    local space = manager.machine.devices[":maincpu"].spaces["program"]
    local map = space.map
    if not map then log("no map") return end
    log("entries " .. #map.entries)
    for i, e in ipairs(map.entries) do
        local r = e.read
        local w = e.write
        local rs = "nil"
        local ws = "nil"
        if r then rs = tostring(r.handlertype) .. "/" .. tostring(r.name) .. "/" .. tostring(r.bits) end
        if w then ws = tostring(w.handlertype) .. "/" .. tostring(w.name) .. "/" .. tostring(w.bits) end
        log(string.format("%04X-%06X mir=%06X mask=%04X R=%s W=%s share=%s region=%s roff=%s",
            i, e.address_start, e.address_end, e.address_mirror, e.address_mask, rs, ws,
            tostring(e.share), tostring(e.region), tostring(e.region_offset)))
    end
end, "frame")
