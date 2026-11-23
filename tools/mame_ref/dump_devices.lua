local frame = 0
local function log(msg) emu.print_info(msg) end
emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= 700 then return end
    log("==== devices ====")
    for tag, dev in pairs(manager.machine.devices) do
        log("DEV " .. tostring(tag))
        if dev.state ~= nil then
            local names = {}
            for name, entry in pairs(dev.state) do names[#names+1] = name end
            table.sort(names)
            log("  state: " .. table.concat(names, " "))
        end
    end
    log("==== regions ====")
    for tag, region in pairs(manager.machine.memory.regions) do
        log(string.format("REGION %s size=%d width=%d", tostring(tag), region.size, region.bitwidth))
    end
    log("==== shares ====")
    for tag, share in pairs(manager.machine.memory.shares) do
        log(string.format("SHARE %s size=%d", tostring(tag), share.size))
    end
    local mapper = manager.machine.devices[":mapper"]
    if mapper and mapper.state then
        for name, entry in pairs(mapper.state) do
            local v = entry.value
            if type(v) == "number" then log(string.format("MAPPER %s = %d (0x%X)", name, v, v)) end
        end
    end
end, "frame")
