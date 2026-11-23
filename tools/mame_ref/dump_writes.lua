-- Record every distinct write address/value pair for the first 700 frames.
local writes = {}
local count = 0
local frame = 0

local function log(msg) emu.print_info(msg) end

local space = manager.machine.devices[":maincpu"].spaces["program"]
space:install_write_tap(0x000000, 0xFFFFFF, "record", function(offset, data, mask)
    local key = string.format("%06X:%04X:%04X", offset, data, mask)
    if writes[key] == nil then
        writes[key] = true
        count = count + 1
    end
    return nil
end)

emu.register_frame_done(function()
    frame = frame + 1
    if frame ~= 700 then return end
    log("==== distinct writes: " .. count .. " ====")
    local keys = {}
    for key in pairs(writes) do keys[#keys + 1] = key end
    table.sort(keys)
    for _, key in ipairs(keys) do
        local addr = tonumber(string.sub(key, 1, 6), 16)
        -- mapper registers are reached in unmapped space; show everything outside the big RAM blocks
        if addr < 0x010000 or addr >= 0x800000 then
            log("WR " .. key)
        end
    end
end, "frame")
