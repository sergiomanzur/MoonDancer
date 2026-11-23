-- Capture reference screenshots at fixed frames and dump a little state.
local frame = 0
local times = { 60, 180, 300, 420, 600, 900, 1200, 1500, 1800, 2100, 2400, 2700, 3000, 3300, 3600 }
local wanted = {}
for _, t in ipairs(times) do wanted[t] = true end

local function log(msg)
    emu.print_info(msg)
end

emu.register_frame_done(function()
    frame = frame + 1
    if wanted[frame] then
        manager.machine.video:snapshot()
        local cpu = manager.machine.devices[":maincpu"]
        if cpu then
            local pc = cpu.state["PC"].value
            local sr = cpu.state["SR"].value
            log(string.format("frame %d PC=%06X SR=%04X", frame, pc, sr))
        end
    end
end, "frame")
